/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-05-24                  the first version
 */

#include <rthw.h>       /* RT-Thread 底层硬件接口（中断开关等） */
#include <rtthread.h>   /* RT-Thread 内核 API */
#include "usart.h"      /* 让控制台输出能用到 huart1 */

/* ============ USART1 接收环形缓冲 ============
 * 中断（USART1_IRQHandler）每收到一个字节就存进这个缓冲区，
 * FinSH 命令线程再慢慢取走。这样即使 ctrl 线程在刷 0,0,0，
 * 串口发来的一整串命令也不会丢字节。 */
#define UART1_RX_BUF_SIZE   256u        /* 缓冲区字节数（256 字节够存好几条命令） */
static volatile uint16_t uart1_rx_head = 0u;    /* 中断写指针：下一个字节存哪里 */
static volatile uint16_t uart1_rx_tail = 0u;    /* 命令线程读指针：下一个字节取哪里 */
static volatile uint8_t  uart1_rx_buf[UART1_RX_BUF_SIZE];  /* 实际缓冲数组 */

/* 在 USART1_IRQHandler 中断里调用：把刚收到的字节存入环形缓冲 */
void rt_uart1_rx_isr(void)
{
    /* 如果发生过载（上一字节还没被取走，新字节就到了），
       读一次 DR 清掉 ORE 错误标志，否则可能反复进中断 */
    if ((USART1->SR & USART_SR_ORE) != 0u)
    {
        (void)USART1->DR;   /* 读 DR 会硬件自动清 ORE */
    }

    /* RXNE=1 表示接收寄存器里有一个完整的新字节 */
    if ((USART1->SR & USART_SR_RXNE) != 0u)
    {
        uint8_t d = (uint8_t)(USART1->DR & 0xFFu);   /* 取出这个字节 */
        uint16_t next = (uint16_t)((uart1_rx_head + 1u) % UART1_RX_BUF_SIZE);
        if (next != uart1_rx_tail)   /* 缓冲区没满才存入；满了就丢弃新字符 */
        {
            uart1_rx_buf[uart1_rx_head] = d;
            uart1_rx_head = next;
        }
    }
}

/* 非阻塞取一个字符：有数据返回 0~255，没有数据返回 -1 */
int rt_uart1_getchar(void)
{
    if (uart1_rx_head == uart1_rx_tail)
        return -1;                       /* 缓冲空：没有新字符 */

    uint8_t d = uart1_rx_buf[uart1_rx_tail];  /* 读走最旧的一个字节 */
    uart1_rx_tail = (uint16_t)((uart1_rx_tail + 1u) % UART1_RX_BUF_SIZE);
    return (int)d;
}

#if defined(RT_USING_USER_MAIN) && defined(RT_USING_HEAP)
/*
 * Please modify RT_HEAP_SIZE if you enable RT_USING_HEAP
 * the RT_HEAP_SIZE max value = (sram size - ZI size), 1024 means 1024 bytes
 */
#define RT_HEAP_SIZE (15*1024)              /* 给 RT-Thread 动态内存 15KB */
static rt_uint8_t rt_heap[RT_HEAP_SIZE];    /* 实际的堆数组，放在静态内存里 */

/* 返回堆的开始地址（RT_WEAK：用户可覆盖） */
RT_WEAK void *rt_heap_begin_get(void)
{
    return rt_heap;        /* 首地址就是数组名 */
}

/* 返回堆的结束地址 */
RT_WEAK void *rt_heap_end_get(void)
{
    return rt_heap + RT_HEAP_SIZE;   /* 数组起始 + 字节数 = 结尾 */
}
#endif

/* 这是“RT-Thread 系统节拍”的回调：SysTick 中断里调用它 */
void rt_os_tick_callback(void)
{
    rt_interrupt_enter();    /* 通知内核：现在进入了中断上下文 */
    
    rt_tick_increase();      /* 让系统 tick +1，驱动延时/时间片/定时器 */

    rt_interrupt_leave();    /* 退出中断上下文 */
}

/**
 * This function will initial your board.
 */
void rt_hw_board_init(void)
{
    /*
     * OS Tick Configuration
     * Use SysTick as the RT-Thread OS tick.  The HAL time base is TIM6
     * (stm32f4xx_hal_timebase_tim.c), so SysTick is free for RT-Thread.
     * SystemClock_Config() in main() changes the core clock afterwards,
     * therefore SysTick is reconfigured there with the final clock.
     * PendSV/SysTick priority is set to the lowest level by
     * rt_hw_context_switch_to() in context_rvds.S.
     */
    /* 给 SysTick 设置中断周期：1 秒 ÷ RT_TICK_PER_SECOND(1000) = 1ms */
    SysTick_Config(SystemCoreClock / RT_TICK_PER_SECOND);

    /* Call components board initial (use INIT_BOARD_EXPORT()) */
#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();   /* 执行所有 INIT_BOARD_EXPORT 注册的初始化 */
#endif

#if defined(RT_USING_USER_MAIN) && defined(RT_USING_HEAP)
    /* 把堆交给 RT-Thread，之后 rt_malloc/rt_thread_create 才能用 */
    rt_system_heap_init(rt_heap_begin_get(), rt_heap_end_get());
#endif
}

#ifdef RT_USING_CONSOLE

/* USART1 is configured by CubeMX generated MX_USART1_UART_Init() inside
 * main().  main() sets this flag to 1 after that, so the console callbacks
 * below are safe even if they are invoked during early RT-Thread startup. */
volatile int rt_uart1_ready = 0;   /* 0=UART1 还没初始化；1=可以发数据 */

/* 板级 UART 初始化：真正的初始化在 main() 的 MX_USART1_UART_Init() 里做 */
static int uart_init(void)
{
    return 0;      /* 这里只是占一个 BOARD_EXPORT 位置 */
}
INIT_BOARD_EXPORT(uart_init);

/* RT-Thread 控制台输出函数：rt_kprintf 最终都会走到这里 */
void rt_hw_console_output(const char *str)
{
    if (!rt_uart1_ready)   /* UART1 还没初始化好，先不输出，防止崩溃 */
        return;            /* 直接丢弃早期启动信息 */

    while (*str != '\0')   /* 逐字符发送整条字符串 */
    {
        if (*str == '\n')          /* Windows/串口助手习惯 CRLF */
        {
            char cr = '\r';        /* 先补一个回车 \r */
            HAL_UART_Transmit(&huart1, (uint8_t *)&cr, 1, 100);
        }
        HAL_UART_Transmit(&huart1, (uint8_t *)str, 1, 100);  /* 发当前字符 */
        str++;                     /* 指向下一个字符 */
    }
}

#endif

