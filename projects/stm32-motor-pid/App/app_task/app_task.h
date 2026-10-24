#ifndef __APP_TASK_H__     /* 头文件保护 */
#define __APP_TASK_H__

#include "rtthread.h"      /* RT-Thread：rt_err_t 等类型在这里 */

/* 目标转速上限。GA12-N20 6V 电机约 150rpm，防止设出不可能的值 */
#define APP_MAX_RPM     150.0f

/* 定义“命令”的种类：bt/命令线程根据 type 决定改哪个参数 */
typedef enum
{
    CMD_SET_RPM = 1,   /* 设置目标转速 */
    CMD_SET_KP,        /* 设置比例系数 */
    CMD_SET_KI,        /* 设置积分系数 */
    CMD_SET_KD,        /* 设置微分系数 */
    CMD_SET_PWM_A,     /* 开环测试：A 电机直接给固定 PWM */
    CMD_SET_PWM_B      /* 开环测试：B 电机直接给固定 PWM */
} CmdType;

void    App_Init(void);                                   /* 创建队列/锁/线程 */
rt_err_t App_PostCommand(CmdType type, float value);      /* 投递一条命令 */

#endif /* APP_RT_H */
