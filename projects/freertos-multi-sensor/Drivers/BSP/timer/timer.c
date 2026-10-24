#include "timer.h"
#include "led.h"

//这个结构体下面都会用到，我们定义成全局的
TIM_HandleTypeDef timer_handle = {0}; //定时器句柄

//定时器初始化函数
//要配置 ARR 和 PSC 寄存器，都是 16 位的寄存器
void timer_init(uint16_t arr, uint16_t psc)
{
	timer_handle.Instance = TIM2; //用到哪一个定时器，用任何一个都可以，我们用定时器2
	timer_handle.Init.Prescaler = psc; //预分频系数
	timer_handle.Init.Period = arr; //重装载值
	timer_handle.Init.CounterMode = TIM_COUNTERMODE_UP; //计数模式为向上计数
	timer_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE; //不使用自动重装载
	
	HAL_TIM_Base_Init(&timer_handle);
	
	HAL_TIM_Base_Start_IT(&timer_handle);//使能更新中断，启动计数器
}

//msp 函数
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
	//各种定时器都会去调用 HAL_TIM_Base_MspInit() 函数，所以我们要先判断是否是定时器2
	if(htim->Instance == TIM2)
	{
		//进行MSP的初始化
		//开启定时器2 时钟
		__HAL_RCC_TIM2_CLK_ENABLE();
		//设置终端优先级，用的是定时器2的中断线，只有一个定时器，抢占优先级和响应优先级随便，都设为 2
		HAL_NVIC_SetPriority(TIM2_IRQn, 2, 2);
		//使能中断
		HAL_NVIC_EnableIRQ(TIM2_IRQn);
	}
}

//中断服务函数
void TIM2_IRQHandler(void)
{
	//HAL 库中，中断服务函数一般会去调用中断服务公共处理函数
	HAL_TIM_IRQHandler(&timer_handle);
}

//更新中断回调函数
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	//这个回调函数也可能被其他定时器调用，我们要判断一下
	if(htim->Instance == TIM2)
	{
		led1_toggle();
	}
}

