#include "delay.h"
#include "FreeRTOS.h"
#include "task.h"


extern void xPortSysTickHandler(void);

//FreeRTOS 系统滴答中断处理函数
void SysTick_Handler(void)
{
		HAL_IncTick();
		//如果系统调度器的状态不是没有启动状态(表示系统已经启动)
		if(xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) //OS(操作系统) 开始跑了，才执行正常的调度处理
		{
			xPortSysTickHandler(); //处理任务延时、时间片轮转调度
		}
}

//初始化 SysTick 定时器，用于 FreeRTOS 系统节拍
void delay_init(void)
{
	SysTick_Config(HAL_RCC_GetHCLKFreq()/configTICK_RATE_HZ); 
}

/**
  * @brief  微秒级延时
  * @param  nus 延时时长，范围：0~233015
  * @retval 无
  */
void delay_us(uint32_t nus)
{
    //定义几个变量
		uint32_t ticks; //一共需要计多少个数
		//tcnt: 已经计了多少个数，told：上次循环 VAL 的值， tnow：当前 VAL 的值
		uint32_t tcnt = 0, told, tnow; 
		uint32_t reload = SysTick->LOAD; //获取系统提供的默认重装载值
		
		ticks = 72 * nus; //一共需要计的数
		told = SysTick->VAL; //刚进入 while 循环时计数器的值
	
		while(1)
		{
			tnow = SysTick->VAL; //获取现在计数器的值
			if(tnow != told)
			{
				if(tnow < told)
					tcnt += told - tnow;
				else
					tcnt += reload - (tnow - told);
				
				told = tnow; //下次进入 while 循环时，当前 VAL 的值作为 told
				
				if(tcnt >= ticks) //已经计的数超过/等于需要计的数，退出循环
					break;
			}
		}	
		
}

/**
  * @brief  毫秒级延时
  * @param  nms 延时时长，范围：0~4294967295
  * @retval 无
  */
void delay_ms(uint32_t nms)
{
    while(nms--)
        delay_us(1000);
}
 
/**
  * @brief  秒级延时
  * @param  ns 延时时长，范围：0~4294967295
  * @retval 无
  */
void delay_s(uint32_t ns)
{
    while(ns--)
        delay_ms(1000);
}

/**
  * @brief  重写HAL_Delay函数
  * @param  nms 延时时长，范围：0~4294967295
  * @retval 无
  */
void HAL_Delay(uint32_t nms)
{
    delay_ms(nms);
}
