#include "rtthread.h"      /* RT-Thread 线程/互斥量/队列 API */
#include "main.h"          /* HAL 库 */
#include "usart.h"         /* 让这里能用 extern huart1 发串口 */
#include "bsp_motor.h"     /* 电机驱动函数 Motor_SetPwm 等 */
#include "bsp_encoder.h"   /* 编码器函数 */
#include "pid.h"           /* PID 结构体和算法 */
#include "app_task.h"      /* 自己的头文件：CmdType/App_Init 等声明 */
#include <stdio.h>         /* sprintf（格式化字符串用） */
#include <stdlib.h>        /* atof（把命令行数字字符串转成 float） */
#include <string.h>        /* strcmp：标定命令里比较字符串用 */

/* ---------------- 可调参数 ---------------- */
#define CTRL_PRIO   5    /* 控制线程优先级：数字小=优先级高，5 比 bt 高 */
#define BT_PRIO     10   /* 命令处理线程优先级 */
#define CTRL_PERIOD (RT_TICK_PER_SECOND / 100)  /* 1000/100=10 个 tick=10ms */
#define REPORT_EVERY_N_LOOPS  10                /* 10ms×10=每100ms上报一次 */
#define HOST_EVERY_N_LOOPS    2                 /* 上位机模式：10ms×2=每20ms一帧 */
#define PID_OUT_MIN (-1000.0f)                  /* PID 输出下限（PWM 逻辑量） */
#define PID_OUT_MAX ( 1000.0f)                  /* PID 输出上限 */

/* ---------------- 命令消息结构 ---------------- */
typedef struct
{
    CmdType type;    /* 告诉接收者：这条命令要改什么 */
    float   value;   /* 命令携带的数值：转速、PID 系数或 PWM */
} CmdMsg;

/* ---------------- 控制模式 ---------------- */
typedef enum
{
    CTRL_MODE_PID = 0,  /* 默认：PID 闭环 */
    CTRL_MODE_OPEN      /* 开环：直接给 PWM，用于先验证电机和方向 */
} CtrlMode;

/* ---------------- 共享资源（全部用 g_param_mtx 保护） ---------------- */
static struct rt_mutex   g_param_mtx;      /* 一把锁，防止两个线程同时改参数 */
static float             g_target_rpm = 0.0f;  /* 目标转速，上电默认 0（不转） */
/* PID 初始增益：已按本电机实测写入默认值
 * （5V 满速约 142rpm、12PPR×4×50=2400 标定后）。
 * kp=6、ki=2：启动快、几十毫秒级响应；抖动时再在串口用 kp/ki/kd 微调。 */
static float             g_kp = 6.0f;      /* PID 比例系数（实测调好后的默认值） */
static float             g_ki = 2.0f;      /* PID 积分系数 */
static float             g_kd = 0.0f;      /* PID 微分系数 */
static CtrlMode          g_ctrl_mode = CTRL_MODE_PID; /* 当前控制模式 */
static int16_t           g_open_pwm_a = 0; /* 开环模式下 A 电机的 PWM */
static int16_t           g_open_pwm_b = 0; /* 开环模式下 B 电机的 PWM */
static int               g_report_enable = 1;  /* 1=每100ms上报一次；0=静音 */
static int               g_dsp_enable = 0;     /* 1=Qt上位机数据流模式；0=普通模式 */

/* ---------------- 消息队列（静态分配内存） ---------------- */
static struct rt_messagequeue g_cmd_mq;    /* 命令队列对象本身 */
static union                     /* 给队列分配一块底层存储 */
{
    rt_uint32_t align;                     /* 让这块内存按 4 字节对齐 */
    rt_uint8_t  bytes[8u * sizeof(CmdMsg)];/* 最多放 8 条 CmdMsg */
} g_cmd_pool;

/* ---------------- 线程控制块 + 栈（静态创建） ---------------- */
static struct rt_thread g_ctrl_tcb;       /* ctrl 线程控制块 */
static rt_uint8_t       g_ctrl_stack[2048];/* ctrl 线程自己的栈 2KB */

static struct rt_thread g_bt_tcb;         /* bt 线程控制块 */
static rt_uint8_t       g_bt_stack[1024]; /* bt 线程栈 1KB */

/* ---------------- 小工具：串口上报 ---------------- */
/* 普通模式：人看的 "A,B,目标" 三行数字 */
static void report_speed(float rpmA, float rpmB, float target)
{
    char buf[48];   /* 临时放一行文本的缓冲区 */
    /* 把三个数字格式化成一串，例如 "150,148,150\r\n" */
    int  n = sprintf(buf, "%.0f,%.0f,%.0f\r\n", rpmA, rpmB, target);
    if (n > 0)      /* sprintf 成功生成了内容 */
        /* 通过 USART1 发出去；20 是超时毫秒数 */
        HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 20);
}

/* Qt 上位机模式：带 $D, 行首标识的一帧，Qt 只解析这种行 */
static void report_host_frame(float rpmA, float rpmB, float target,
                              int16_t outA, int16_t outB)
{
    char buf[64];   /* 缓冲区：一行最多 60 字节左右 */
    int  n = sprintf(buf, "$D,%lu,%.0f,%.0f,%.0f,%d,%d\r\n",
                     (unsigned long)HAL_GetTick(),  /* 开机以来的毫秒数 */
                     rpmA, rpmB, target,
                     (int)outA, (int)outB);         /* 当前实际 PWM */
    if (n > 0)
        HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 20);
}

/*
 * ============ [C] 电机控制线程：每 10ms 跑一次 ============
 */
static void ctrl_thread_entry(void *param)
{
    Pid_t pidA, pidB;   /* A、B 两台电机各自独立的 PID 状态 */

    (void)param;        /* 本线程不用参数，这句只是消除编译器警告 */
    /* 用当前全局增益初始化两台电机的 PID */
    Pid_Init(&pidA, g_kp, g_ki, g_kd, PID_OUT_MIN, PID_OUT_MAX);
    Pid_Init(&pidB, g_kp, g_ki, g_kd, PID_OUT_MIN, PID_OUT_MAX);

    while (1)           /* 线程主体：永远循环 */
    {
        /* [C1] 记下本次循环开始的时刻，后面用来对齐 10ms */
        rt_tick_t start = rt_tick_get();
        /* 下面这些是“每轮要用的局部快照变量” */
        float target, kp, ki, kd;      /* 目标转速 + PID 三个增益 */
        float rpmA, rpmB, outA, outB;  /* 实测转速 + 要输出的 PWM */
        int16_t deltaA, deltaB;        /* 两个编码器测到的脉冲增量 */
        int16_t openA = 0, openB = 0;  /* 开环 PWM 快照 */
        int report_enable = 1;         /* 本轮的“是否允许上报”快照 */
        int dsp_enable = 0;            /* 本轮的“Qt数据流模式”快照 */
        CtrlMode mode = CTRL_MODE_PID; /* 当前模式快照 */
        static rt_uint8_t prev_open = 0;  /* 记上一次是否开环（static 不会每轮重置） */
        static rt_uint8_t report_cnt = 0; /* 距上次上报过了几轮 */

        /* [C2] 加锁，把共享参数复制成局部变量（快照）。
           这样即使 bt 线程同时改参数，这一轮也不会读到半新半旧的值 */
        rt_mutex_take(&g_param_mtx, RT_WAITING_FOREVER);
        target = g_target_rpm;   /* 目标转速 */
        kp = g_kp;               /* Kp */
        ki = g_ki;               /* Ki */
        kd = g_kd;               /* Kd */
        mode = g_ctrl_mode;      /* PID 还是开环 */
        openA = g_open_pwm_a;    /* 开环 A 值 */
        openB = g_open_pwm_b;    /* 开环 B 值 */
        report_enable = g_report_enable;  /* 是否刷转速（rep 0 可关闭） */
        dsp_enable = g_dsp_enable;        /* Qt 数据流模式开关（dsp 1/0） */
        rt_mutex_release(&g_param_mtx);   /* 读完立刻解锁，别一直占着 */

        /* [C3] 读两个编码器在这 10ms 内的脉冲增量（内部会清零） */
        deltaA = Encoder_GetDelta(MOTOR_A);
        deltaB = Encoder_GetDelta(MOTOR_B);

        /* [C4] 脉冲增量 → RPM。dt 是 10ms，换算成秒就是 0.01 */
        rpmA = Encoder_DeltaToRpm(deltaA, ENCODER_A_COUNTS_PER_REV,
                                  (float)CTRL_PERIOD / (float)RT_TICK_PER_SECOND);
        rpmB = Encoder_DeltaToRpm(deltaB, ENCODER_B_COUNTS_PER_REV,
                                  (float)CTRL_PERIOD / (float)RT_TICK_PER_SECOND);

        /* [C5] 根据模式算出这一轮要给电机的 PWM */
        if (mode == CTRL_MODE_OPEN)      /* 开环测试模式 */
        {
            /* 第一次进入开环时，把 PID 历史清掉，防止一会切回来时乱跳 */
            if (prev_open == 0u)
            {
                Pid_Reset(&pidA);
                Pid_Reset(&pidB);
            }
            outA = (float)openA;   /* 不经过 PID，直接用用户给的值 */
            outB = (float)openB;
        }
        else                           /* PID 闭环模式 */
        {
            /* 从开环切回闭环的第一轮，先把 PID 历史清零 */
            if (prev_open != 0u)
            {
                Pid_Reset(&pidA);
                Pid_Reset(&pidB);
            }
            /* 把最新的 Kp/Ki/Kd 写进 PID 对象 */
            Pid_SetGains(&pidA, kp, ki, kd);
            Pid_SetGains(&pidB, kp, ki, kd);
            /* 用“目标转速 vs 实际转速”算 PWM 输出 */
            outA = Pid_Update(&pidA, target, rpmA, (float)CTRL_PERIOD / (float)RT_TICK_PER_SECOND);
            outB = Pid_Update(&pidB, target, rpmB, (float)CTRL_PERIOD / (float)RT_TICK_PER_SECOND);
        }
        /* 记录“这一轮是不是开环”，供下一轮判断模式切换 */
        prev_open = (mode == CTRL_MODE_OPEN) ? 1u : 0u;

        /* [C6] 把算出的 PWM 输出给两路电机 */
        Motor_SetPwm(MOTOR_A, (int16_t)outA);
        Motor_SetPwm(MOTOR_B, (int16_t)outB);

        /* [C7] 上报：Qt 数据流模式每 2 轮（20ms）发 $D 帧；
               普通模式每 10 轮（100ms）发 "0,0,0" 便于人看 */
        if (dsp_enable)
        {
            if (++report_cnt >= HOST_EVERY_N_LOOPS)
            {
                report_cnt = 0;
                report_host_frame(rpmA, rpmB, target,
                                  (int16_t)outA, (int16_t)outB);
            }
        }
        else if (report_enable && ++report_cnt >= REPORT_EVERY_N_LOOPS)
        {
            report_cnt = 0;                       /* 计数清零重新数 */
            report_speed(rpmA, rpmB, target);     /* 发串口 */
        }

        /* [C8] 对齐周期：如果这轮跑得比 10ms 快，就睡到满 10ms 再下一轮 */
        {
            int32_t remain = (int32_t)CTRL_PERIOD - (int32_t)(rt_tick_get() - start);
            if (remain > 0)                 /* 还有剩余时间就休眠 */
                rt_thread_mdelay((rt_tick_t)remain);
        }
    }
}

/*
 * ============ [B] 命令处理线程：谁投递命令，它就来改共享参数 ============
 */
static void bt_thread_entry(void *param)
{
    CmdMsg msg;      /* 每轮收到的命令 */

    (void)param;
    while (1)
    {
        /* [B1][B2] 阻塞等待：队列里没有命令就挂起，不浪费 CPU */
        if (rt_mq_recv(&g_cmd_mq, &msg, sizeof(msg),
                       RT_WAITING_FOREVER) != RT_EOK)
            continue;                       /* 理论到不了，防御一下 */

        /* [B3] 加锁：接下来要修改控制线程也在读的共享参数 */
        rt_mutex_take(&g_param_mtx, RT_WAITING_FOREVER);
        switch (msg.type)                   /* 看这条命令是什么类型 */
        {
        case CMD_SET_RPM:                   /* [B4] 设置目标转速 */
            /* 限幅：不允许负转速，也不允许超过电机上限 */
            if (msg.value < 0.0f)
                g_target_rpm = 0.0f;        /* 小于 0 就当 0 */
            else if (msg.value > APP_MAX_RPM)
                g_target_rpm = APP_MAX_RPM; /* 大于 150 就当 150 */
            else
                g_target_rpm = msg.value;   /* 正常值直接使用 */
            g_ctrl_mode = CTRL_MODE_PID;    /* 设转速 = 要闭环，切回 PID */
            break;

        case CMD_SET_KP:                    /* 改比例系数 */
            if (msg.value >= 0.0f) g_kp = msg.value;  /* 负数不允许 */
            break;

        case CMD_SET_KI:                    /* 改积分系数 */
            if (msg.value >= 0.0f) g_ki = msg.value;
            break;

        case CMD_SET_KD:                    /* 改微分系数 */
            if (msg.value >= 0.0f) g_kd = msg.value;
            break;

        case CMD_SET_PWM_A:                 /* 开环 A：直接给固定 PWM */
        case CMD_SET_PWM_B:                 /* 开环 B：直接给固定 PWM */
            /* 先把数值限制在 ±1000 内 */
            if (msg.value > (float)MOTOR_PWM_MAX)
                msg.value = (float)MOTOR_PWM_MAX;
            if (msg.value < -(float)MOTOR_PWM_MAX)
                msg.value = -(float)MOTOR_PWM_MAX;
            if (msg.type == CMD_SET_PWM_A)  /* 存进对应电机的开环值 */
                g_open_pwm_a = (int16_t)msg.value;
            else
                g_open_pwm_b = (int16_t)msg.value;
            g_ctrl_mode = CTRL_MODE_OPEN;   /* 切到开环模式 */
            break;

        default:                            /* 未知命令直接忽略 */
            break;
        }
        rt_mutex_release(&g_param_mtx);     /* [B5] 处理完释放锁 */
    }
}

/*
 * ============ [A] 应用初始化 ============
 * 调用前提：外设已初始化、RT-Thread 调度器已在运行。
 * main() 里先做好 HAL/MX 初始化，再调用本函数。
 */
void App_Init(void)
{
    /* 初始化命令队列：名字 cmdq、底层内存 g_cmd_pool、
       每条消息 sizeof(CmdMsg)、池总大小、FIFO 规则 */
    rt_mq_init(&g_cmd_mq, "cmdq", &g_cmd_pool.bytes[0],
               sizeof(CmdMsg), sizeof(g_cmd_pool.bytes),
               RT_IPC_FLAG_FIFO);

    /* 初始化共享参数锁：名字 pidmtx，采用优先级继承规则 */
    rt_mutex_init(&g_param_mtx, "pidmtx", RT_IPC_FLAG_PRIO);

    /* 静态创建 ctrl 线程：入口 ctrl_thread_entry，
       栈 g_ctrl_stack、大小 2048、优先级 5、时间片 10 tick */
    rt_thread_init(&g_ctrl_tcb, "ctrl", ctrl_thread_entry, RT_NULL,
                   &g_ctrl_stack[0], sizeof(g_ctrl_stack),
                   CTRL_PRIO, 10);
    rt_thread_startup(&g_ctrl_tcb);         /* 把它加入就绪队列 */

    /* 同样创建 bt 命令线程 */
    rt_thread_init(&g_bt_tcb, "bt", bt_thread_entry, RT_NULL,
                   &g_bt_stack[0], sizeof(g_bt_stack),
                   BT_PRIO, 10);
    rt_thread_startup(&g_bt_tcb);

    Motor_Enable();   /* 最后才 STBY=1：此时 PWM 已启动且占空比=0，安全 */
}

/* 通用投递命令接口：任何线程/中断想控制电机，就调它 */
rt_err_t App_PostCommand(CmdType type, float value)
{
    CmdMsg msg;      /* 把参数打包成一条消息 */
    msg.type = type;     /* 命令类型 */
    msg.value = value;   /* 命令数值 */
    /* 投进队列，让 bt 线程去处理；返回成功/失败 */
    return rt_mq_send(&g_cmd_mq, &msg, sizeof(msg));
}

/* ============ FinSH/MSH 串口命令 ============
 * 在串口助手里输入 rpm 100、pwm a 500、kp 1.5 等即可执行。
 * MSH_CMD_EXPORT 的作用：把这几个函数注册成 msh 命令。
 */
static int rpm(int argc, char **argv)   /* 命令：rpm <目标转速> */
{
    float v;                            /* 存解析出的目标转速 */

    if (argc < 2)                       /* 没带参数：argc 至少要有命令名+1 个数 */
    {
        rt_kprintf("usage: rpm <0..%d>\r\n", (int)APP_MAX_RPM);
        return -1;
    }
    v = (float)atof(argv[1]);           /* 把输入字符串转成 float */
    if (App_PostCommand(CMD_SET_RPM, v) == RT_EOK)  /* 投递给命令线程 */
    {
        rt_kprintf("target rpm ok\r\n");
        return 0;
    }
    rt_kprintf("queue full\r\n");       /* 队列满了 */
    return -1;
}
MSH_CMD_EXPORT(rpm, "set target rpm (PID mode)");  /* 注册成 msh 命令 */

static int pwm(int argc, char **argv)   /* 命令：pwm <a|b> <值> */
{
    CmdType type;                       /* 决定 A 还是 B */
    float v;                            /* PWM 数值 */

    /* 参数检查：要 3 个参数，且第二个必须是 a/A/b/B */
    if (argc < 3 ||
        (argv[1][0] != 'a' && argv[1][0] != 'A' &&
         argv[1][0] != 'b' && argv[1][0] != 'B'))
    {
        rt_kprintf("usage: pwm <a|b> <pwm -1000..1000>\r\n");
        return -1;
    }
    /* 根据字母选命令类型 */
    type = (argv[1][0] == 'a' || argv[1][0] == 'A') ?
           CMD_SET_PWM_A : CMD_SET_PWM_B;
    v = (float)atof(argv[2]);           /* 转成数字 */
    if (App_PostCommand(type, v) == RT_EOK)
    {
        rt_kprintf("open-loop pwm ok\r\n");
        return 0;
    }
    rt_kprintf("queue full\r\n");
    return -1;
}
MSH_CMD_EXPORT(pwm, "open-loop PWM test, e.g. pwm a 800");

static int kp(int argc, char **argv)    /* 命令：kp <值> */
{
    if (argc < 2)
    {
        rt_kprintf("usage: kp <value>\r\n");
        return -1;
    }
    if (App_PostCommand(CMD_SET_KP, (float)atof(argv[1])) == RT_EOK)
    {
        rt_kprintf("kp ok\r\n");
        return 0;
    }
    return -1;
}
MSH_CMD_EXPORT(kp, "set PID kp");

static int ki(int argc, char **argv)    /* 命令：ki <值> */
{
    if (argc < 2)
    {
        rt_kprintf("usage: ki <value>\r\n");
        return -1;
    }
    if (App_PostCommand(CMD_SET_KI, (float)atof(argv[1])) == RT_EOK)
    {
        rt_kprintf("ki ok\r\n");
        return 0;
    }
    return -1;
}
MSH_CMD_EXPORT(ki, "set PID ki");

static int kd(int argc, char **argv)    /* 命令：kd <值> */
{
    if (argc < 2)
    {
        rt_kprintf("usage: kd <value>\r\n");
        return -1;
    }
    if (App_PostCommand(CMD_SET_KD, (float)atof(argv[1])) == RT_EOK)
    {
        rt_kprintf("kd ok\r\n");
        return 0;
    }
    return -1;
}
MSH_CMD_EXPORT(kd, "set PID kd");

/* ============ 标定/断电专用命令 ============ */

/* 让驱动芯片进入待机：电机断电可自由转动，供“手转标定”用 */
static int motor_free(int argc, char **argv)
{
    (void)argc;               /* 本命令不需要参数 */
    (void)argv;
    /* 自动关掉转速刷屏，让标定输出干净好读 */
    rt_mutex_take(&g_param_mtx, RT_WAITING_FOREVER);
    g_report_enable = 0;
    rt_mutex_release(&g_param_mtx);
    Motor_Disable();          /* STBY=0，TB6612 停止输出 */
    rt_kprintf("motor free, report off: rotate the shaft now\r\n");
    return 0;
}
MSH_CMD_EXPORT(motor_free, "stop driving and let motor shaft turn freely");

/* 恢复驱动：标定完后把 STBY 重新拉高 */
static int motor_on(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    rt_mutex_take(&g_param_mtx, RT_WAITING_FOREVER);
    g_report_enable = 1;      /* 恢复每100ms上报 */
    rt_mutex_release(&g_param_mtx);
    Motor_Enable();           /* STBY=1，恢复 TB6612 输出 */
    rt_kprintf("motor enable ok\r\n");
    return 0;
}
MSH_CMD_EXPORT(motor_on, "re-enable motor driver (STBY=1)");

/* 刷屏开关：rep 0 = 关掉转速上报，rep 1 = 恢复 */
static int rep(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("usage: rep <0|1>  (0=stop reporting, 1=start)\r\n");
        return -1;
    }
    rt_mutex_take(&g_param_mtx, RT_WAITING_FOREVER);
    g_report_enable = (argv[1][0] == '1') ? 1 : 0;
    rt_mutex_release(&g_param_mtx);
    rt_kprintf(g_report_enable ? "report on\r\n" : "report off\r\n");
    return 0;
}
MSH_CMD_EXPORT(rep, "enable/disable speed reporting: rep <0|1>");

/* Qt 上位机数据流：dsp 1 = 每20ms发一条 $D,xxx 帧；dsp 0 = 退回普通模式 */
static int dsp(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("usage: dsp <1|0>  (1=Qt data stream, 0=normal)\r\n");
        return -1;
    }
    rt_mutex_take(&g_param_mtx, RT_WAITING_FOREVER);
    g_dsp_enable = (argv[1][0] == '1') ? 1 : 0;
    if (g_dsp_enable)
        g_report_enable = 0;          /* 开 Qt 模式后不再发 0,0,0 人看行 */
    else
        g_report_enable = 1;          /* 退出 Qt 模式恢复普通上报 */
    rt_mutex_release(&g_param_mtx);
    rt_kprintf(g_dsp_enable ? "data stream on\r\n" : "data stream off\r\n");
    return 0;
}
MSH_CMD_EXPORT(dsp, "Qt host data stream: dsp <1|0>");

/* 标定命令：cal <a|b> <reset|read>
 *   用法：motor_free 后，cal a reset，
 *         用手把输出轴转满 10 圈，cal a read，
 *         读到的总数 ÷ 10 = 每转脉冲数 */
static int cal(int argc, char **argv)
{
    MotorId id;               /* 选择 A 还是 B */

    if (argc < 3)             /* 至少要 cal a reset 这样 3 个词 */
    {
        rt_kprintf("usage: cal <a|b> <reset|read>\r\n");
        return -1;
    }
    /* 第二个词 a/A → 电机 A，b/B → 电机 B */
    if (argv[1][0] == 'a' || argv[1][0] == 'A')
        id = MOTOR_A;
    else if (argv[1][0] == 'b' || argv[1][0] == 'B')
        id = MOTOR_B;
    else
    {
        rt_kprintf("usage: cal <a|b> <reset|read>\r\n");
        return -1;
    }

    if (strcmp(argv[2], "reset") == 0)      /* 清零累计脉冲 */
    {
        Encoder_CalReset(id);
        rt_kprintf("cal counter reset ok\r\n");
        return 0;
    }
    if (strcmp(argv[2], "read") == 0)       /* 读出累计脉冲 */
    {
        rt_kprintf("cal total=%ld\r\n", (long)Encoder_CalRead(id));
        return 0;
    }

    rt_kprintf("usage: cal <a|b> <reset|read>\r\n");
    return -1;
}
MSH_CMD_EXPORT(cal, "encoder calibration: cal <a|b> <reset|read>");
