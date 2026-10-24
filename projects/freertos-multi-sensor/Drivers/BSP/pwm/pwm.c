#include "pwm.h"

TIM_HandleTypeDef pwm_handle = {0}; //全局句柄

//初始化函数
void pwm_init(uint16_t arr, uint16_t psc) //参数和上一个实验一样
{
	TIM_OC_InitTypeDef pwm_config = {0};
	
	pwm_handle.Instance = TIM4; //用到哪一个定时器，用任何一个都可以，我们用定时器4
	pwm_handle.Init.Prescaler = psc; //预分频系数
	pwm_handle.Init.Period = arr; //重装载值
	pwm_handle.Init.CounterMode = TIM_COUNTERMODE_UP; //计数模式为向上计数
	pwm_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE; //不使用自动重装载
	HAL_TIM_PWM_Init(&pwm_handle);
	
	pwm_config.OCMode = TIM_OCMODE_PWM1; //使用 PWM 模式1
	pwm_config.Pulse = arr/2; //CCR 的初值
	pwm_config.OCPolarity = TIM_OCPOLARITY_LOW;//有效电平为低电平
	
	HAL_TIM_PWM_ConfigChannel(&pwm_handle, &pwm_config, TIM_CHANNEL_3);
	
	HAL_TIM_PWM_Start(&pwm_handle, TIM_CHANNEL_3);
}

//msp 函数
void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
	//是否是第4个定时器
	if(htim->Instance == TIM4)
	{
		//配置 GPIO，中断，时钟
		//初始化 GPIO 口
		GPIO_InitTypeDef gpio_initstruct;
		//打开时钟
		__HAL_RCC_GPIOB_CLK_ENABLE(); //使能 GPIOB 时钟
		//开启定时器4时钟
		__HAL_RCC_TIM4_CLK_ENABLE();
		
		//调用 GPIO 初始化函数
		gpio_initstruct.Pin = GPIO_PIN_8; //LED1 对应的引脚
		gpio_initstruct.Mode = GPIO_MODE_AF_PP; //推挽复用输出
		gpio_initstruct.Pull = GPIO_PULLUP; //上拉
		gpio_initstruct.Speed = GPIO_SPEED_FREQ_HIGH; //高速
		
		HAL_GPIO_Init(GPIOB, &gpio_initstruct);
		
		//设置中断优先级，用的是定时器4的中断线，只有一个定时器，抢占优先级和响应优先级随便，都设为 2
		HAL_NVIC_SetPriority(TIM4_IRQn, 2, 2);
		//使能中断
		HAL_NVIC_EnableIRQ(TIM4_IRQn);
	}
}

//修改 CCR 寄存器值的函数
void set_pwm_compare(uint16_t val)
{
	__HAL_TIM_SET_COMPARE(&pwm_handle, TIM_CHANNEL_3, val);
}

