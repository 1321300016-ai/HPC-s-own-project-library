#include "key.h"
#include "delay.h"

//初始化 GPIO
void key_init(void)
{
	GPIO_InitTypeDef gpio_initstruct;
	//打开时钟
	__HAL_RCC_GPIOA_CLK_ENABLE(); //使能 GPIOA 时钟
	//调用 GPIO 初始化函数
	gpio_initstruct.Pin = GPIO_PIN_0 | GPIO_PIN_1; //两个 KEY 对应的引脚
	gpio_initstruct.Mode = GPIO_MODE_INPUT; //输入
	gpio_initstruct.Pull = GPIO_PULLUP; //上拉
	gpio_initstruct.Speed = GPIO_SPEED_FREQ_HIGH; //高速
	HAL_GPIO_Init(GPIOB, &gpio_initstruct);
}

//按键扫描函数
uint8_t key_scan(void)
{
	//检查按键是否按下
	if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) //如果读到低电平
	{
		//消抖
		delay_ms(10);
		//再次判断按键是否按下
		if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) //如果读到低电平
		{
			//如果确实是按下的状态，等待按键松开
			while(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET);
			//返回按键值
			return 1;
		}
	}
	//检查按键是否按下
	if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET) //如果读到低电平
	{
		//消抖
		delay_ms(10);
		//再次判断按键是否按下
		if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET) //如果读到低电平
		{
			//如果确实是按下的状态，等待按键松开
			while(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET);
			//返回按键值
			return 2;
		}
	}
	//返回默认值
	return 0;
}
