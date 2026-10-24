#ifndef __DHT11_H__
#define __DHT11_H__

#include "sys.h"

/* DHT11 数据脚：PA1（单总线，需要 4.7k~10k 上拉电阻） */
#define DHT11_GPIO_PORT     GPIOA
#define DHT11_GPIO_PIN      GPIO_PIN_1
#define DHT11_GPIO_CLK()    __HAL_RCC_GPIOA_CLK_ENABLE()

/* 一次读取得到的温湿度原始值 */
typedef struct
{
    uint8_t humi_int;   /* 湿度整数部分 */
    uint8_t humi_dec;   /* 湿度小数部分（DHT11 恒为 0） */
    uint8_t temp_int;   /* 温度整数部分 */
    uint8_t temp_dec;   /* 温度小数部分 */
} DHT11_Data_t;

void    dht11_init(void);
uint8_t dht11_read(DHT11_Data_t *data);   /* 返回 0 成功，1 校验失败，2 超时 */

#endif
