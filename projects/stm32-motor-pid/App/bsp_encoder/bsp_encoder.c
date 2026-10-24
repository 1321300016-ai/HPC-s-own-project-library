#include "bsp_encoder.h"   /* 自己的头文件：声明了下面这些函数 */
#include "bsp_motor.h"     /* 用到 MOTOR_A / MOTOR_B 枚举 */
#include "main.h"          /* HAL 库 */
#include "tim.h"           /* 声明 extern htim3 / htim4 定时器句柄 */

/* ============ 标定累加器 ============
 * 平时 ctrl 线程每 10ms 读一次增量并清零定时器。
 * 标定时虽然每 10ms 仍在清，但我们把每次增量都累加到这里，
 * 这样“转 N 圈”期间的总脉冲数就不会丢。 */
static int32_t cal_count_a = 0;   /* 电机 A 编码器的累计脉冲 */
static int32_t cal_count_b = 0;   /* 电机 B 编码器的累计脉冲 */

/* 根据电机编号，返回它对应的编码器定时器句柄 */
/* 约定：TIM3 接电机 A 编码器，TIM4 接电机 B 编码器 */
static TIM_HandleTypeDef *encoder_tim(MotorId id)
{
    /* 是 MOTOR_A 就返回 &htim3，否则返回 &htim4 */
    return (id == MOTOR_A) ? &htim3 : &htim4;
}

/* ============ [E0] 编码器初始化 ============ */
void Encoder_Init(void)
{
    /* 让 TIM3 进入编码器模式并开始计数（能加也能减） */
    HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);
    /* 让 TIM4 也进入编码器模式 */
    HAL_TIM_Encoder_Start(&htim4, TIM_CHANNEL_ALL);

    /* 计数器从 0 开始，方便以后读“增量” */
    __HAL_TIM_SET_COUNTER(&htim3, 0);
    __HAL_TIM_SET_COUNTER(&htim4, 0);
}

/* ============ [E1] 清某个编码器 ============ */
void Encoder_Reset(MotorId id)
{
    TIM_HandleTypeDef *htim = encoder_tim(id);  /* 先找到对应定时器 */
    __HAL_TIM_SET_COUNTER(htim, 0);             /* 把计数值写成 0 */
}

/* ============ [E2] 读增量并清零 ============ */
int16_t Encoder_GetDelta(MotorId id)
{
    TIM_HandleTypeDef *htim = encoder_tim(id);  /* 找到对应定时器 */

    /* 把 0~65535 的计数器值看成有符号数：
     * 0xFFFF 转成 int16_t 就是 -1，表示反转了 1 个脉冲 */
    int16_t raw = (int16_t)__HAL_TIM_GET_COUNTER(htim);

    __HAL_TIM_SET_COUNTER(htim, 0);             /* 立刻清零，开始计下一段 */

    /* 顺手累加到标定计数器：正转加、反转减，净脉冲数 = 实际转的脉冲 */
    if (htim->Instance == TIM3)
        cal_count_a += raw;
    else
        cal_count_b += raw;

    return raw;                                 /* 返回这一段的脉冲增量 */
}

/* ============ [E4] 标定：清零累加器 ============ */
void Encoder_CalReset(MotorId id)
{
    if (id == MOTOR_A)
        cal_count_a = 0;
    else
        cal_count_b = 0;
}

/* ============ [E5] 标定：读累加器 ============ */
int32_t Encoder_CalRead(MotorId id)
{
    /* 返回从上次 Encoder_CalReset 到现在累计的净脉冲数 */
    return (id == MOTOR_A) ? cal_count_a : cal_count_b;
}

/* ============ [E3] 脉冲增量换算成 RPM ============ */
float Encoder_DeltaToRpm(int16_t delta, uint32_t counts_per_rev, float dt_sec)
{
    /* 防止除零：没标定(0)或时间不对时，直接返回 0 */
    if (counts_per_rev == 0u || dt_sec <= 0.0f)
        return 0.0f;

    /* 思路：
     * 1) delta / counts_per_rev = 这段时间转了几圈
     * 2) 再除以 dt_sec            = 每秒转几圈（rps）
     * 3) 再乘以 60                = 每分钟转几圈（rpm）
     */
    return ((float)delta / (float)counts_per_rev) / dt_sec * 60.0f;
}
