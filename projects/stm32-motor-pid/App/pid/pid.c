#include "pid.h"     /* 先看 pid.h：里面有 Pid_t 结构和所有函数声明 */

/* ============ P0 初始化 ============ */
void Pid_Init(Pid_t *pid, float kp, float ki, float kd,
              float out_min, float out_max)
{
    /* pid 是一个“指向 Pid_t 结构体”的指针，-> 就是访问它里面的成员 */
    pid->kp = kp;             /* 存比例系数 */
    pid->ki = ki;             /* 存积分系数 */
    pid->kd = kd;             /* 存微分系数 */
    pid->integral = 0.0f;     /* 积分从 0 开始，还没有历史误差 */
    pid->prev_err = 0.0f;     /* “上一次误差”也置 0，微分项不会乱跳 */
    pid->out_min  = out_min;  /* 记住输出下限 */
    pid->out_max  = out_max;  /* 记住输出上限 */
}

/* ============ P1 在线改增益 ============ */
void Pid_SetGains(Pid_t *pid, float kp, float ki, float kd)
{
    /* 只改三个系数。积分不清零，这样电机转动中调参不会突然抖动 */
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

/* ============ P2 复位 ============ */
void Pid_Reset(Pid_t *pid)
{
    /* 从开环切回闭环、重新启动时，把“历史”清掉重新计算 */
    pid->integral = 0.0f;     /* 积分归零 */
    pid->prev_err = 0.0f;     /* 上次误差归零 */
}

/*
 * ============ P3 核心：PID 更新一次 ============
 * 调用者每 10ms 调一次，每次给它：
 *   setpoint = 目标转速（用户想要的 RPM）
 *   feedback = 实际转速（编码器测出来的 RPM）
 *   dt       = 距离上一次调用的时间（秒），这里是 0.01
 * 它返回一个 -1000 ~ +1000 的 PWM 输出
 */
float Pid_Update(Pid_t *pid, float setpoint, float feedback, float dt)
{
    float err, out;           /* err=本次误差；out=将要返回的输出 */

    /* 防止有人误传 0 或负数时间；时间无效就用 1 秒，避免除零 */
    if (dt <= 0.0f)
        dt = 1.0f;

    /* [P3.1] 误差 = 目标 - 实际。误差>0 说明转速慢了，要多给油 */
    err = setpoint - feedback;

    /* [P3.2] 位置式 PID：
     *   P 项 = kp × err：误差越大，输出越大
     *   I 项 = ki × integral：把“攒下来的误差”算进去，消除稳态误差
     *   D 项 = kd × (err-prev_err)/dt：误差变化越快，输出越早刹车
     */
    out = pid->kp * err
        + pid->ki * pid->integral
        + pid->kd * (err - pid->prev_err) / dt;

    /* [P3.3] 输出限幅：结果不能超过电机允许的 ±1000 */
    if (out > pid->out_max)      /* 太大了，压到上限 */
        out = pid->out_max;
    else if (out < pid->out_min) /* 太小了，抬到下限 */
        out = pid->out_min;

    /* [P3.4] 抗积分饱和：
     * 只有输出还没到上下限时，才允许积分继续累积；
     * 如果输出已经饱和（比如顶到 1000），再累积积分会导致恢复慢。
     */
    if (out < pid->out_max && out > pid->out_min)
        pid->integral += err * dt;   /* 积分 = 历史误差 × 时间 的累加 */

    /* [P3.5] 把本次误差记下来，下次算 D 项要用 */
    pid->prev_err = err;

    return out;   /* 把算好的 PWM 输出交还给调用者 */
}
