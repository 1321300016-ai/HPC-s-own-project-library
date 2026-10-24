#include "dht11.h"
#include "delay.h"
#include "FreeRTOS.h"
#include "task.h"

/* 把数据脚切成“推挽输出”模式，主机要主动拉低/释放总线 */
static void dht11_pin_output(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = DHT11_GPIO_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_GPIO_PORT, &gpio);
}

/* 把数据脚切成输入模式，并打开内部上拉，读传感器回应的电平 */
static void dht11_pin_input(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = DHT11_GPIO_PIN;
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(DHT11_GPIO_PORT, &gpio);
}

static uint8_t dht11_pin_read(void)
{
    return (HAL_GPIO_ReadPin(DHT11_GPIO_PORT, DHT11_GPIO_PIN) == GPIO_PIN_SET) ? 1 : 0;
}

/* 读 1 个 bit：传感器用高电平持续时间区分 0 和 1 */
static uint8_t dht11_read_bit(void)
{
    uint16_t retry = 0;

    while (dht11_pin_read() == 0 && retry++ < 200)   /* 等待 50us 低电平结束 */
        delay_us(1);

    retry = 0;
    while (dht11_pin_read() == 1 && retry++ < 200)   /* 统计高电平持续时间 */
        delay_us(1);

    /* 高电平 26~28us 是 0，约 70us 是 1（含循环开销，阈值取 40） */
    if (retry > 40)
        return 1;
    return 0;
}

static uint8_t dht11_read_byte(void)
{
    uint8_t i, byte = 0;
    for (i = 0; i < 8; i++)
    {
        byte <<= 1;
        byte |= dht11_read_bit();
    }
    return byte;
}

void dht11_init(void)
{
    DHT11_GPIO_CLK();
    dht11_pin_output();
    HAL_GPIO_WritePin(DHT11_GPIO_PORT, DHT11_GPIO_PIN, GPIO_PIN_SET);
}

/*
 * 读取一次温湿度：
 *   1) 主机拉低 18ms 以上作为起始信号
 *   2) 释放总线，传感器回应 40bit 数据
 *   3) 第 5 字节是前 4 字节之和，用于校验
 */
uint8_t dht11_read(DHT11_Data_t *data)
{
    uint8_t buf[5];
    uint8_t i;
    uint16_t retry;

    if (data == NULL)
        return 2;

    /* 读取期间挂起调度器，避免任务切换打断单总线时序
       （中断仍然开着，不会丢系统节拍） */
    vTaskSuspendAll();

    dht11_pin_output();
    HAL_GPIO_WritePin(DHT11_GPIO_PORT, DHT11_GPIO_PIN, GPIO_PIN_RESET);
    delay_ms(20);                                     /* 起始信号：拉低至少 18ms */
    HAL_GPIO_WritePin(DHT11_GPIO_PORT, DHT11_GPIO_PIN, GPIO_PIN_SET);
    delay_us(30);                                     /* 主机释放总线 20~40us */
    dht11_pin_input();

    retry = 0;
    while (dht11_pin_read() == 1 && retry++ < 300)    /* 等传感器拉低回应 */
        delay_us(1);
    if (retry >= 300) { xTaskResumeAll(); return 2; }

    retry = 0;
    while (dht11_pin_read() == 0 && retry++ < 300)    /* 等低电平结束 */
        delay_us(1);
    if (retry >= 300) { xTaskResumeAll(); return 2; }

    retry = 0;
    while (dht11_pin_read() == 1 && retry++ < 300)    /* 等高电平结束，数据开始 */
        delay_us(1);
    if (retry >= 300) { xTaskResumeAll(); return 2; }

    for (i = 0; i < 5; i++)
        buf[i] = dht11_read_byte();

    xTaskResumeAll();

    /* 校验：前四字节之和的低 8 位 == 第五字节 */
    if ((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) != buf[4])
        return 1;

    data->humi_int = buf[0];
    data->humi_dec = buf[1];
    data->temp_int = buf[2];
    data->temp_dec = buf[3];
    return 0;
}
