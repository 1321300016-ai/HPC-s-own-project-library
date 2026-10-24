#include "bsp_motor.h"   /* 自己的头文件 */
#include "tim.h"         /* 让本文件能看见 extern TIM_HandleTypeDef htim1 */
#include "main.h"        /* HAL 库 */

/*
 * 天空星 F407VET6 的引脚分配（和 CubeMX 里的配置一一对应）：
 *   AIN1/AIN2 = PE7/PE8     TB6612 的电机 A 方向输入
 *   BIN1/BIN2 = PE10/PE12   TB6612 的电机 B 方向输入
 *   STBY      = PE13        TB6612 总使能
 *   PWM       = TIM1_CH1(PE9) 给 A，TIM1_CH2(PE11) 给 B
 */
#define MOTOR_A_IN1_PORT   GPIOE
#define MOTOR_A_IN1_PIN    GPIO_PIN_7
#define MOTOR_A_IN2_PORT   GPIOE
#define MOTOR_A_IN2_PIN    GPIO_PIN_8

#define MOTOR_B_IN1_PORT   GPIOE
#define MOTOR_B_IN1_PIN    GPIO_PIN_10
#define MOTOR_B_IN2_PORT   GPIOE
#define MOTOR_B_IN2_PIN    GPIO_PIN_12

#define MOTOR_STBY_PORT    GPIOE
#define MOTOR_STBY_PIN     GPIO_PIN_13

/* 把 PWM 限制在 ±MOTOR_PWM_MAX（±1000）内，防止写坏硬件 */
static int16_t clamp_pwm(int16_t pwm)
{
    if (pwm > MOTOR_PWM_MAX)    /* 正方向太大会钳到 +1000 */
        pwm = MOTOR_PWM_MAX;
    if (pwm < -MOTOR_PWM_MAX)   /* 反方向太大会钳到 -1000 */
        pwm = -MOTOR_PWM_MAX;
    return pwm;                 /* 返回安全范围内的值 */
}

/* ============ [M0] 电机模块初始化 ============ */
void Motor_Init(void)
{
    /* STBY 先拉低：TB6612 处于待机，此时电机无论怎么都不转（安全） */
    HAL_GPIO_WritePin(MOTOR_STBY_PORT, MOTOR_STBY_PIN, GPIO_PIN_RESET);

    /* 四个方向脚全部拉低：IN1=0、IN2=0，属于停止状态 */
    HAL_GPIO_WritePin(GPIOE,
                      MOTOR_A_IN1_PIN | MOTOR_A_IN2_PIN |
                      MOTOR_B_IN1_PIN | MOTOR_B_IN2_PIN,
                      GPIO_PIN_RESET);

    /* 占空比先清 0：即使后面误使能，也没有功率输出 */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);

    /* 关键一步：把 TIM1 的 CH1/CH2 PWM 真正“启动”起来；
       不启动的话 PE9/PE11 上不会有波形，电机永远不会转 */
    if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK)
        Error_Handler();   /* 启动失败直接进死循环报错 */
    if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2) != HAL_OK)
        Error_Handler();
}

/* ============ [M1] 使能驱动芯片 ============ */
void Motor_Enable(void)
{
    /* STBY=1：TB6612 解除待机，此时方向脚和 PWM 才真正驱动电机 */
    HAL_GPIO_WritePin(MOTOR_STBY_PORT, MOTOR_STBY_PIN, GPIO_PIN_SET);
}

/* ============ [M2] 关闭驱动芯片（紧急停止） ============ */
void Motor_Disable(void)
{
    /* STBY=0：整个驱动芯片进入待机，电机立刻失去驱动 */
    HAL_GPIO_WritePin(MOTOR_STBY_PORT, MOTOR_STBY_PIN, GPIO_PIN_RESET);
    /* 顺手把占空比也清 0，双保险 */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);
}

/*
 * ============ [M3] 设置单路电机 ============
 * 对外接口：pwm 是 -1000~+1000
 *   pwm > 0 ：正转（IN1=1, IN2=0），占空比=pwm
 *   pwm < 0 ：反转（IN1=0, IN2=1），占空比=|pwm|
 *   pwm = 0 ：两个方向脚都置 1，短接制动
 */
void Motor_SetPwm(MotorId id, int16_t pwm)
{
    GPIO_PinState in1 = GPIO_PIN_RESET;   /* 先默认 IN1=0 */
    GPIO_PinState in2 = GPIO_PIN_RESET;   /* 先默认 IN2=0 */
    uint32_t compare;                     /* 最后写入定时器 CCR 的值 */

    pwm = clamp_pwm(pwm);   /* 第一步：先保证 pwm 在安全范围 */

    /* 第二步：根据正负号决定两个方向脚的电平 */
    if (pwm > 0)               /* 正转 */
    {
        in1 = GPIO_PIN_SET;    /* IN1=1 */
        in2 = GPIO_PIN_RESET;  /* IN2=0 */
    }
    else if (pwm < 0)          /* 反转 */
    {
        pwm = (int16_t)(-pwm); /* 取绝对值，后面只处理大小 */
        in1 = GPIO_PIN_RESET;  /* IN1=0 */
        in2 = GPIO_PIN_SET;    /* IN2=1 */
    }
    else                       /* pwm == 0：刹车 */
    {
        in1 = GPIO_PIN_SET;    /* IN1=IN2=1 是 TB6612 的短接制动 */
        in2 = GPIO_PIN_SET;
    }

    /* 第三步：把 -1000~+1000 的“逻辑占空比”换算成定时器 CCR 值。
       TIM1 的 ARR=8399，如果不换算，1000 只代表 1000/8400≈11.9%；
       换算后 1000 就代表接近 100% 占空比。 */
    compare = ((uint32_t)pwm * htim1.Init.Period) / (uint32_t)MOTOR_PWM_MAX;

    /* 第四步：写方向脚 + 写 PWM 比较寄存器 */
    if (id == MOTOR_A)   /* 操作 A 电机 */
    {
        HAL_GPIO_WritePin(MOTOR_A_IN1_PORT, MOTOR_A_IN1_PIN, in1);
        HAL_GPIO_WritePin(MOTOR_A_IN2_PORT, MOTOR_A_IN2_PIN, in2);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, compare);
    }
    else                 /* 否则就是 B 电机 */
    {
        HAL_GPIO_WritePin(MOTOR_B_IN1_PORT, MOTOR_B_IN1_PIN, in1);
        HAL_GPIO_WritePin(MOTOR_B_IN2_PORT, MOTOR_B_IN2_PIN, in2);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, compare);
    }
}

/* ============ [M4] 停止单路电机 ============ */
void Motor_Stop(MotorId id)
{
    Motor_SetPwm(id, 0);   /* 其实就是“PWM=0 + 制动” */
}
