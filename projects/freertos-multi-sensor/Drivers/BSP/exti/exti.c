#include "exti.h"
#include "sys.h"
#include "delay.h"
#include "led.h"

uint8_t ia_flag = FALSE; //检测到红外标志位

void exti_init(void) //初始化中断
{
	GPIO_InitTypeDef gpio_initstruct;
	//打开时钟
	__HAL_RCC_GPIOB_CLK_ENABLE(); //使能 GPIOB 时钟
	
	//调用GPIO 初始化函数
	gpio_initstruct.Pin = GPIO_PIN_4; //红外传感器对应的引脚
	gpio_initstruct.Mode = GPIO_MODE_IT_FALLING; //下降沿触发
	gpio_initstruct.Pull = GPIO_PULLUP; //默认上拉
	HAL_GPIO_Init(GPIOB, &gpio_initstruct);
	
	HAL_NVIC_SetPriority(EXTI4_IRQn, 2, 0); //设置 EXTI4 中断线的优先级
	HAL_NVIC_EnableIRQ(EXTI4_IRQn); //使能中断
}

void EXTI4_IRQHandler(void) //中断服务函数
{
	HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4); //中断服务公共函数
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) //回调函数
{
	if(GPIO_Pin == GPIO_PIN_4)
	{
		if(HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_RESET)
			ia_flag = TRUE;
	}
}

//获取红外状态
uint8_t get_ia_flag(void)
{
	uint8_t temp = ia_flag;
	ia_flag = FALSE; //获取到红外标志位后将其清除，否则标志位一直为 TRUE
	return temp; 
}

void set_ia_flag(uint8_t value) //设置红外状态
{
	ia_flag = value;
}
