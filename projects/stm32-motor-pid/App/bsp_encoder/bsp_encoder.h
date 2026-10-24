#ifndef BSP_ENCODER_H     /* 头文件保护 */
#define BSP_ENCODER_H

#include "main.h"         /* 里面有 stm32f4xx_hal.h，能认识 HAL 类型 */
#include <stdint.h>       /* 提供 int16_t / uint32_t 这些定长整数类型 */
#include "bsp_motor.h"    /* 使用 MotorId（MOTOR_A / MOTOR_B）这个枚举 */

/*
 * 每转一圈，编码器到底给多少个脉冲？
 * 计算公式：COUNTS_PER_REV = 编码器 PPR × 倍频(本工程 4 倍) × 减速比
 * 本电机：12 PPR × 4 × 50 = 2400
 * 验证依据：满 PWM 实测旧显示 184 → 对应真实转速约 145rpm，
 *          符合 6V 空载 150rpm、5V 供电略低一档的标称。
 */
#ifndef ENCODER_A_COUNTS_PER_REV
#define ENCODER_A_COUNTS_PER_REV   2400  /* 电机 A：12PPR × 4 × 50 减速 */
#endif

#ifndef ENCODER_B_COUNTS_PER_REV
#define ENCODER_B_COUNTS_PER_REV   2400  /* 电机 B：12PPR × 4 × 50 减速 */
#endif

/* 启动 TIM3/TIM4 编码器模式 */
void    Encoder_Init(void);
/* 把某个编码器计数器清零 */
void    Encoder_Reset(MotorId id);
/* 标定用：清空电机编码器的“累计脉冲” */
void    Encoder_CalReset(MotorId id);
/* 标定用：读出电机编码器自上次清零以来的累计净脉冲 */
int32_t Encoder_CalRead(MotorId id);
/* 读出“这一小段时间”的脉冲增量，然后自动清零（有正负号） */
int16_t Encoder_GetDelta(MotorId id);
/* 把脉冲增量换算成 RPM 转速 */
float   Encoder_DeltaToRpm(int16_t delta, uint32_t counts_per_rev, float dt_sec);

#endif /* BSP_ENCODER_H */
