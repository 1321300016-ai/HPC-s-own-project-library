#ifndef PID_H        /* 头文件保护：防止同一个 .h 被重复包含 */
#define PID_H

/* PID 控制器的“数据结构”，把某一台电机的 PID 状态存成一个对象 */
typedef struct
{
    float kp;         /* 比例系数：误差放大多少倍 */
    float ki;         /* 积分系数：历史误差累积的权重 */
    float kd;         /* 微分系数：误差变化率的权重 */

    float integral;   /* 积分累积值 = 每个周期误差 × 时间 的累加 */
    float prev_err;   /* 上一次的误差，微分项需要用它算“误差变化了多少” */

    float out_min;    /* 输出下限：PWM 最低只能到多少（这里是 -1000） */
    float out_max;    /* 输出上限：PWM 最高只能到多少（这里是 +1000） */
} Pid_t;

/* 初始化：把系数、输出范围写进去，并把积分/上次误差清零 */
void  Pid_Init(Pid_t *pid, float kp, float ki, float kd,
               float out_min, float out_max);
/* 运行时修改 Kp/Ki/Kd（串口调参用），不清积分 */
void  Pid_SetGains(Pid_t *pid, float kp, float ki, float kd);
/* 复位历史状态（从开环切回闭环、或重新起转时用） */
void  Pid_Reset(Pid_t *pid);
/* 核心函数：给定目标转速和实际转速，算出一个 PWM 输出 */
float Pid_Update(Pid_t *pid, float setpoint, float feedback, float dt);

#endif /* PID_H */
