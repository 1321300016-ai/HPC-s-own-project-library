# Qt 双电机 PID 监控

## 功能

- 自动扫描并连接串口，默认 `115200 8N1`。
- 实时显示双电机实际转速、目标转速和 PWM 曲线。
- 快捷发送 `rpm`、`pwm`、`dsp` 等固件命令。
- 支持导出波形数据为 CSV。

## 构建

要求 Qt 5.8 或兼容版本，并安装 `widgets`、`charts` 和 `serialport` 模块：

```bash
qmake MotorMonitor.pro
make
```

也可直接用 Qt Creator 打开 `MotorMonitor.pro`。`motor_step_data.csv` 是示例数据。

配套固件见 `../stm32-motor-pid/`，连接固件 USART1。
