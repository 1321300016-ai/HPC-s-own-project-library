# FreeRTOS 多传感器采集与本地监控

## 功能

- STM32F103，系统时钟 72 MHz。
- DHT11 温湿度采集，软件 I2C OLED 本地显示。
- USART1 `115200 8N1`，DMA + 空闲中断接收命令。
- FreeRTOS 四任务模型：采集、命令处理、显示、运行状态监控。
- 使用队列、互斥量、任务通知、软件定时器与栈水位统计。

## 硬件接口

- DHT11 数据脚：PA1。
- OLED 软件 I2C：PB8 / PB9。
- 串口：USART1，115200 bps。

具体引脚以 `Drivers/BSP` 与 `Projects/MDK-ARM/stm32f103.uvprojx` 为准。

## 构建

使用 Keil MDK 打开：

```text
Projects/MDK-ARM/stm32f103.uvprojx
```

编译后下载到目标板。工程构建目录与历史输出未提交，首次打开后由 Keil 重新生成。

## 串口命令

| 命令 | 功能 |
| --- | --- |
| `GET` | 查询最近一次温湿度与错误计数 |
| `RATE 1000` | 设置采样周期为 1000 ms |
| `LED ON` / `LED OFF` | 控制板载 LED |
| `HELP` | 打印命令帮助 |
