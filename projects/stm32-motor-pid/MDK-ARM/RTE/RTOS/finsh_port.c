/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 */

#include <rthw.h>        /* RT-Thread 硬件抽象层 */
#include <rtconfig.h>    /* RT-Thread 配置：里面会 include finsh_config.h */
#include "usart.h"       /* 访问 huart1（USART1 句柄） */

/* board.c 里实现的环形缓冲取字符函数：有字符返回 0~255，没有返回 -1 */
extern int rt_uart1_getchar(void);

#ifndef RT_USING_FINSH
/* 若没在 rtconfig.h 里放开 finsh_config.h，直接编译报错提醒 */
#error Please uncomment the line <#include "finsh_config.h"> in the rtconfig.h 
#endif

#ifdef RT_USING_FINSH

/* FinSH 命令行“读一个字符”的底层实现。
 * RT_WEAK：允许用户在其他地方写一个更强的同名函数覆盖它 */
RT_WEAK char rt_hw_console_getchar(void)
{
    /* 初始值必须 < 0，表示“还没有读到字符” */
    int ch = -1;

    while (ch < 0)      /* 没读到就继续等 */
    {
        /* 从环形缓冲区取字符（字符由 USART1 接收中断提前放进来，
           不会因为 ctrl 线程正在刷 0,0,0 而丢失） */
        ch = rt_uart1_getchar();
        if (ch < 0)
        {
            /* 没有数据时睡 10ms，别占着 CPU 空转 */
            rt_thread_mdelay(10);
        }
    }

    return ch;    /* 把读到的字符交给 FinSH 解析 */
}

#endif /* RT_USING_FINSH */

