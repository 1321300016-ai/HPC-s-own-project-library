#ifndef BSP_MOTOR_H     /* 头文件保护 */
#define BSP_MOTOR_H

#include "main.h"       /* HAL 库：GPIO/TIM 相关类型 */
#include <stdint.h>     /* int16_t 等定长类型 */

/* PWM 的“逻辑量程”。电机模块对外只说 -1000~+1000，
   真正的定时器占空比换算在 Motor_SetPwm 内部完成 */
#define MOTOR_PWM_MAX   1000

/* 用枚举给两个电机起名字，比记 0/1 好懂 */
typedef enum
{
    MOTOR_A = 0,   /* 电机 A：TIM1_CH1(PE9) */
    MOTOR_B        /* 电机 B：TIM1_CH2(PE11) */
} MotorId;

void    Motor_Init(void);                    /* 初始化方向脚并启动 PWM 通道 */
void    Motor_Enable(void);                  /* STBY=1，让 TB6612 开始工作 */
void    Motor_Disable(void);                 /* STBY=0，紧急关断 */
void    Motor_SetPwm(MotorId id, int16_t pwm); /* 设置某台电机：方向+占空比 */
void    Motor_Stop(MotorId id);              /* 设置 PWM=0，刹车 */

#endif /* BSP_MOTOR_H */
