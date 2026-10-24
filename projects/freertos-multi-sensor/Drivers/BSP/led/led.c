#include "led.h"
#include "sys.h"

//初始化 GPIO 的函数
void led_init(void)
{
	GPIO_InitTypeDef gpio_initstruct;
	//打开时钟
	__HAL_RCC_GPIOB_CLK_ENABLE(); //使能 GPIOB 时钟
	//调用 GPIO 初始化函数
	gpio_initstruct.Pin = GPIO_PIN_0 | GPIO_PIN_1; //两个 LED 对应的引脚
	gpio_initstruct.Mode = GPIO_MODE_OUTPUT_PP; //推挽输出
	gpio_initstruct.Pull = GPIO_PULLUP; //上拉
	gpio_initstruct.Speed = GPIO_SPEED_FREQ_HIGH; //高速
	HAL_GPIO_Init(GPIOB, &gpio_initstruct);
	
	//默认关闭 LED1 和 LED2
	led1_off();
	led2_off();
}
//点亮 LED1 的函数
void led1_on(void)
{
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); //拉低 LED1 引脚，点亮 LED1
}
//熄灭 LED1 的函数
void led1_off(void)
{
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET); //拉高 LED1 引脚，点亮 LED1
}
//点亮 LED2 的函数
void led2_on(void)
{
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET); //拉低 LED2 引脚，点亮 LED2
}
//熄灭 LED2 的函数
void led2_off(void)
{
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET); //拉高 LED2 引脚，点亮 LED2
}
//翻转 LED1 状态的函数
void led1_toggle(void)
{
	HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0);
}
//翻转 LED2 状态的函数
void led2_toggle(void)
{
	HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_1);
}
