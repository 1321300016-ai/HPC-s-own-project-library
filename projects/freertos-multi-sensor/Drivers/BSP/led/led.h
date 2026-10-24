#ifndef __LED_H__
#define __LED_H__

//初始化 GPIO 的函数
void led_init(void);

//点亮 LED1 的函数
void led1_on(void);

//熄灭 LED1 的函数
void led1_off(void);

//点亮 LED2 的函数
void led2_on(void);

//熄灭 LED2 的函数
void led2_off(void);

//翻转 LED1 状态的函数
void led1_toggle(void);

//翻转 LED2 状态的函数
void led2_toggle(void);

#endif
