#include "sys.h"
#include "delay.h"
#include "led.h"
#include "oled.h"
#include "uart1.h"
#include "dht11.h"
#include "app_tasks.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * 基于 STM32F103 + FreeRTOS 的多传感器采集与本地监控系统
 *   - 采集任务：DHT11 温湿度
 *   - 显示任务：OLED 实时显示
 *   - 指令任务：串口 DMA + 空闲中断收命令
 *   - 监控任务：栈水位 / 堆剩余 / 各任务 CPU 占用率
 */
int main(void)
{
    HAL_Init();                          /* 初始化 HAL */
    stm32_clock_init(RCC_PLL_MUL9);      /* 系统时钟 72MHz */
    delay_init();                        /* 延时 + FreeRTOS SysTick */

    led_init();                          /* 指示灯 */
    oled_init();                         /* OLED（软件 I2C：PB8/PB9） */
    oled_fill(0x00);
    uart1_init(115200);                  /* 串口 115200，DMA+空闲中断接收 */
    dht11_init();                        /* DHT11 数据脚 PA1 */

    app_tasks_start();                   /* 创建 4 个任务、队列、互斥锁、软件定时器 */

    vTaskStartScheduler();               /* 启动 FreeRTOS，不再返回 */

    while (1)
    {
        /* 正常情况下调度器启动后不会执行到这里 */
    }
}
