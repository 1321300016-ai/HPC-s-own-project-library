#ifndef __OLED_H__
#define __OLED_H__

#include "sys.h"

#define OLED_I2C_SCL_CLK()	__HAL_RCC_GPIOB_CLK_ENABLE() //使能 SCL 时钟(GPIOB时钟)
#define OLED_I2C_SCL_PORT GPIOB  //SCL 端口，GPIOB 组
#define OLED_I2C_SCL_PIN GPIO_PIN_8  //SCL 引脚，PB8

#define OLED_I2C_SDA_CLK()	__HAL_RCC_GPIOB_CLK_ENABLE() //使能 SDA 时钟(GPIOB时钟)
#define OLED_I2C_SDA_PORT GPIOB  //SCL 端口，GPIOB 组
#define OLED_I2C_SDA_PIN GPIO_PIN_9  //SCL 引脚，PB9

//SCL 拉低
#define OLED_SCL_RESET() HAL_GPIO_WritePin(OLED_I2C_SCL_PORT, OLED_I2C_SCL_PIN, GPIO_PIN_RESET)
//SCL 拉高
#define OLED_SCL_SET() HAL_GPIO_WritePin(OLED_I2C_SCL_PORT, OLED_I2C_SCL_PIN, GPIO_PIN_SET)

//SDA 拉低
#define OLED_SDA_RESET() HAL_GPIO_WritePin(OLED_I2C_SDA_PORT, OLED_I2C_SDA_PIN, GPIO_PIN_RESET)
//SDA 拉高
#define OLED_SDA_SET() HAL_GPIO_WritePin(OLED_I2C_SDA_PORT, OLED_I2C_SDA_PIN, GPIO_PIN_SET)

//OLED 初始化
void oled_init(void);
//发送一个命令
void oled_write_cmd(uint8_t cmd);
//发送一个数据
void oled_write_data(uint8_t data);
//指定坐标的函数(x:列(0~127),y: PAGE(0~7))
void oled_set_cursor(uint8_t x, uint8_t y);
//清屏函数
void oled_fill(uint8_t data);
//显示字符函数
//x: 列， y：PAGE，num：哪个字符，size：大小，字符高度
void oled_show_char(uint8_t x, uint8_t y, uint8_t num, uint8_t size);
//显示字符串
//x：列，y：PAGE，p：字符串，size：大小，字符高度
void oled_show_string(uint8_t x, uint8_t y, char *p, uint8_t size);
//显示一个汉字
//x：列，y：PAGE，num：哪个汉字，size：大小，汉字有多高
void oled_show_chinese(uint8_t x, uint8_t y, uint8_t num, uint8_t size);
//显示图片
//x：列，y：PAGE，width：图片宽度，height：图片高度，bmp：图片数据
void oled_show_img(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t *bmp);

#endif
