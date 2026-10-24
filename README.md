# HPC 个人项目库

这里整理并展示嵌入式控制、Qt 上位机、实时操作系统和 Linux 网络编程项目。仓库只保留源码、工程配置、必要资源与说明文档，不提交可重新生成的编译产物。

## 项目索引

| 目录 | 项目 | 主要技术 |
| --- | --- | --- |
| [`projects/stm32-motor-pid`](projects/stm32-motor-pid/) | STM32F407 双电机 PID 调速 | STM32 HAL、RT-Thread、PWM、正交编码器、串口 |
| [`projects/qt-motor-monitor`](projects/qt-motor-monitor/) | 双电机 PID 调速上位机 | Qt 5、Qt Charts、Qt SerialPort |
| [`projects/freertos-multi-sensor`](projects/freertos-multi-sensor/) | 多传感器采集与本地监控 | STM32F103、FreeRTOS、DHT11、OLED、DMA 串口 |
| [`projects/linux-router`](projects/linux-router/) | Linux 软件路由器与 IP 过滤 | C、pthread、raw socket、ARP 缓存 |

## 项目说明

### STM32F407 双电机 PID 调速

基于 STM32F407VET6 的双直流电机闭环调速项目。控制周期为 10 ms，使用 TIM3/TIM4 正交编码器接口测量转速，TIM1 生成两路 PWM，通过 USART1 向 Qt 上位机发送实时数据。支持位置式 PID、积分抗饱和、编码器标定、开环 PWM 测试和串口在线调参。

### Qt 双电机监控

与 STM32F407 固件配套的上位机。解析以 `$D,` 开头的数据帧，以曲线显示双电机实际转速、目标转速与 PWM，并可发送测速、开环、PID 参数和采样显示命令，支持导出 CSV 波形数据。

### FreeRTOS 多传感器采集与本地监控

基于 STM32F103 和 FreeRTOS 的本地环境监控系统。创建采集、指令、显示和监控四类任务，使用队列、互斥量、任务通知和软件定时器组织并发；通过 DHT11 采集温湿度，本地 OLED 显示，并可通过串口查询数据、调整采样周期和控制 LED。

### Linux 软件路由器

使用 C 与 pthread 实现的 Linux 软件路由实验项目。包含原始数据包收发、IP 过滤、ARP 缓存、静态内存池、命令行解析和多线程键盘控制；通过 Makefile 构建，运行时通常需要 root 权限和双网卡环境。

## 目录说明

- `projects/stm32-motor-pid`：Keil MDK 工程入口为 `MDK-ARM/Motor_pid.uvprojx`，STM32CubeMX 配置为 `Motor_pid.ioc`。
- `projects/freertos-multi-sensor`：Keil MDK 工程入口为 `Projects/MDK-ARM/stm32f103.uvprojx`。
- `projects/qt-motor-monitor`：Qt 工程入口为 `MotorMonitor.pro`。
- `projects/linux-router`：Linux 下执行 `make` 生成 `router`。

各目录的构建步骤、硬件接口和串口命令见对应 `README.md`。

## 整理原则

- 原始工程中的 `_f103_work`、`_qt_work` 和 `_backup_*` 属于重复工作副本，不进入仓库。
- `.o`、`.exe`、`.axf`、`.hex`、`.map`、`.crf`、Keil `Objects/Listings` 和 Qt 构建目录不进入仓库。
- Qt `.pro.user*` 与 Keil `.uvguix.*` 保存本机 IDE 状态，不进入仓库。
- 第三方 HAL、CMSIS、FreeRTOS 文件保持原样；许可信息见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。

## 来源路径

本次整理对应以下本地位置：

- `E:\app-file\cube-file\Motor_pid`
- `E:\app-file\FreeRTOS-file\6. 多传感器采集与本地监控系统`
- `C:\Users\Administrator\Desktop\router_test`
- `E:\QtProjects\C++` 中的 Qt 构建记录

`E:\QtProjects\C++` 只包含构建目录；其中的 `MotorMonitor` 构建记录指向实际源码 `E:\app-file\QT-file\xinrui\MotorMonitor`，仓库上传的是该源码工程。
