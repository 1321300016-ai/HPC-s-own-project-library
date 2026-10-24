# STM32F407 双电机 PID 调速

## 功能

- STM32F407VET6，系统时钟 168 MHz。
- 10 ms 控制周期，两路直流电机独立 PID 闭环。
- TIM3、TIM4 正交编码器测速；TIM1 输出两路 PWM。
- USART1 以 115200 bps 发送普通调试数据或 Qt 数据帧。
- 支持开环 PWM、编码器标定、在线修改目标转速和 PID 参数。

## 目录

- `App/`：电机、编码器、PID 与应用任务。
- `Core/`：STM32CubeMX 生成的 HAL 初始化与中断代码。
- `Drivers/`：工程实际使用的 HAL、CMSIS 设备头文件与启动依赖。
- `MDK-ARM/Motor_pid.uvprojx`：Keil MDK 工程入口。
- `Motor_pid.ioc`：STM32CubeMX 配置文件。

## 构建

1. 安装 Keil MDK、STM32F4 Device Pack 和 RT-Thread 3.1.5 软件包。
2. 打开 `MDK-ARM/Motor_pid.uvprojx`。
3. 编译并下载到 STM32F407VET6。
4. 将 USART1 连接至 USB-TTL，串口设置为 `115200 8N1`。

RT-Thread 内核和设备组件由 Keil Pack Manager 提供，因此不重复存放在本仓库。

## 常用命令

| 命令 | 功能 |
| --- | --- |
| `rpm 60` | 将双电机闭环目标设为 60 RPM |
| `rpm 0` | 停止 |
| `pwm a 500` | A 电机开环 PWM 输出 500 |
| `kp 6` / `ki 2` / `kd 0` | 在线修改 PID 参数 |
| `rep 0` / `rep 1` | 关闭 / 开启周期上报 |
| `dsp 1` / `dsp 0` | 开启 / 关闭 Qt `$D,` 数据帧 |
| `cal a reset` | A 电机编码器累计值清零 |
| `cal a read` | 读取 A 电机标定累计脉冲 |

普通模式下，固件周期发送 `A转速,B转速,目标转速`。Qt 模式下发送：

```text
$D,毫秒时间, A转速, B转速, 目标转速, PWM_A, PWM_B
```

配套上位机见 `../qt-motor-monitor/`。
