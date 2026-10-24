#include "gate.h"
#include "sys.h"

//初始化 GPIO 的函数
void gate_init(void)
{
	GPIO_InitTypeDef gpio_initstruct;
	//打开时钟
	__HAL_RCC_GPIOB_CLK_ENABLE(); //使能 GPIOB 时钟
	//调用 GPIO 初始化函数
	gpio_initstruct.Pin = GPIO_PIN_7; //继电器对应的引脚
	gpio_initstruct.Mode = GPIO_MODE_OUTPUT_PP; //推挽输出
	gpio_initstruct.Pull = GPIO_PULLUP; //上拉
	gpio_initstruct.Speed = GPIO_SPEED_FREQ_HIGH; //高速
	HAL_GPIO_Init(GPIOB, &gpio_initstruct);
	
	//默认关闭继电器
	gate_off();
}
//闭合的函数
void gate_on(void)
{
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET); //拉低继电器引脚，让继电器闭合
}
//松开继电器
void gate_off(void)
{
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET); //拉高继电器引脚，让继电器松开
}

uint8_t get_gate_status(void) //获取继电器状态
{
	return (uint8_t)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7);
}
