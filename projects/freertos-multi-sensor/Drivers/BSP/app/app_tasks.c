#include "sys.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "uart1.h"
#include "oled.h"
#include "dht11.h"
#include "led.h"
#include "app_tasks.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* ---------------- 任务栈/优先级 ---------------- */
#define SENSOR_TASK_PRIO        3       /* 采集：优先级最高，保证时序 */
#define CMD_TASK_PRIO           3       /* 指令解析 */
#define DISPLAY_TASK_PRIO       1       /* 显示：慢任务放低 */
#define MONITOR_TASK_PRIO       1       /* 状态监控 */

#define SENSOR_STACK_WORDS      256
#define CMD_STACK_WORDS         256
#define DISPLAY_STACK_WORDS     256
#define MONITOR_STACK_WORDS     512     /* 统计字符串较长，多给一点 */

#define DEFAULT_SAMPLE_MS       2000    /* 默认 2s 采集一次 */
#define MONITOR_PERIOD_MS       10000   /* 10s 输出一次状态 */

/* ---------------- 数据结构 ---------------- */
typedef struct
{
    uint8_t valid;      /* 1=本次读取有效 */
    uint8_t temp;       /* 温度，摄氏度 */
    uint8_t humi;       /* 湿度，%RH */
    uint32_t err_cnt;   /* 累计读取失败次数 */
} SensorData_t;

/* ---------------- 全局对象 ---------------- */
static TaskHandle_t  sensor_task_handle  = NULL;
static TaskHandle_t  cmd_task_handle     = NULL;
static TaskHandle_t  display_task_handle = NULL;
static TaskHandle_t  monitor_task_handle = NULL;

static QueueHandle_t sensor_queue = NULL;   /* 采集 → 显示 */
static SemaphoreHandle_t printf_mutex = NULL;  /* 保护串口打印 */
static SemaphoreHandle_t oled_mutex = NULL;    /* 保护 OLED */
static TimerHandle_t sample_timer = NULL;   /* 软件定时器：周期触发采集 */

static volatile uint32_t g_sample_period_ms = DEFAULT_SAMPLE_MS;
static volatile uint32_t g_err_cnt = 0;
static volatile uint8_t  g_last_temp = 0;   /* 最近一次有效温度，供 GET 查询 */
static volatile uint8_t  g_last_humi = 0;   /* 最近一次有效湿度 */

/* ---------------- 带互斥锁的 printf ---------------- */
static void app_printf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (xSemaphoreTake(printf_mutex, portMAX_DELAY) == pdTRUE)
    {
        printf("%s", buf);
        xSemaphoreGive(printf_mutex);
    }
}

/* ---------------- 软件定时器回调 ---------------- */
static void sample_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    /* 定时器回调运行在定时器服务任务里，只做“通知”，不能做耗时操作 */
    if (sensor_task_handle != NULL)
        xTaskNotifyGive(sensor_task_handle);
}

/* ---------------- 1) 采集任务 ---------------- */
static void sensor_task(void *param)
{
    DHT11_Data_t dht;
    SensorData_t data;
    uint8_t last_temp = 0, last_humi = 0;

    (void)param;

    for (;;)
    {
        /* 等软件定时器通知；超时时间给 2 倍采样周期，防止偶发丢通知 */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(g_sample_period_ms * 2 + 100));

        memset(&data, 0, sizeof(data));
        if (dht11_read(&dht) == 0)
        {
            last_temp = dht.temp_int;
            last_humi = dht.humi_int;
            g_last_temp = last_temp;
            g_last_humi = last_humi;
            data.valid = 1;
        }
        else
        {
            g_err_cnt++;
            data.valid = 0;             /* 本次失败，显示上一次的值 */
        }
        data.temp = last_temp;
        data.humi = last_humi;
        data.err_cnt = g_err_cnt;

        /* 队列满就丢最旧：显示数据允许丢帧，不能阻塞采集 */
        xQueueSend(sensor_queue, &data, 0);
    }
}

/* ---------------- 2) 显示任务 ---------------- */
static void display_task(void *param)
{
    SensorData_t data;
    char line[24];

    (void)param;

    if (xSemaphoreTake(oled_mutex, portMAX_DELAY) == pdTRUE)
    {
        oled_fill(0x00);
        oled_show_string(0, 0, "Multi-Sensor", 16);
        oled_show_string(0, 2, "Waiting DHT11", 16);
        xSemaphoreGive(oled_mutex);
    }

    for (;;)
    {
        if (xQueueReceive(sensor_queue, &data, portMAX_DELAY) == pdPASS)
        {
            if (xSemaphoreTake(oled_mutex, portMAX_DELAY) == pdTRUE)
            {
                if (data.valid)
                {
                    snprintf(line, sizeof(line), "Temp: %d C", data.temp);
                    oled_show_string(0, 0, line, 16);
                    snprintf(line, sizeof(line), "Humi: %d %%", data.humi);
                    oled_show_string(0, 2, line, 16);
                    led1_on();
                }
                else
                {
                    oled_show_string(0, 0, "DHT11 ERROR", 16);
                    led1_off();
                }
                snprintf(line, sizeof(line), "Rate: %lums", (unsigned long)g_sample_period_ms);
                oled_show_string(0, 4, line, 16);
                snprintf(line, sizeof(line), "Err: %lu", (unsigned long)data.err_cnt);
                oled_show_string(0, 6, line, 16);

                xSemaphoreGive(oled_mutex);
            }
        }
    }
}

/* ---------------- 3) 指令解析任务 ---------------- */
static void cmd_task(void *param)
{
    UartFrame_t frame;
    char line[UART1_FRAME_MAX + 1];
    char *p;

    (void)param;

    for (;;)
    {
        if (uart1_wait_frame(&frame, portMAX_DELAY) != pdTRUE)
            continue;

        if (frame.len > UART1_FRAME_MAX)
            frame.len = UART1_FRAME_MAX;
        memcpy(line, frame.data, frame.len);
        line[frame.len] = '\0';

        /* 去掉行尾的 \r\n，只保留命令本体 */
        p = strpbrk(line, "\r\n");
        if (p != NULL)
            *p = '\0';

        if (strcmp(line, "GET") == 0)
        {
            app_printf("[GET] temp=%d C humi=%d %% err=%lu\r\n",
                       (int)g_last_temp, (int)g_last_humi,
                       (unsigned long)g_err_cnt);
        }
        else if (strncmp(line, "RATE ", 5) == 0)
        {
            uint32_t ms = (uint32_t)atoi(&line[5]);
            if (ms < 200)
                ms = 200;
            if (ms > 60000)
                ms = 60000;
            g_sample_period_ms = ms;
            xTimerChangePeriod(sample_timer, pdMS_TO_TICKS(ms), 0);
            app_printf("[RATE] sample period = %lums\r\n", (unsigned long)ms);
        }
        else if (strcmp(line, "LED ON") == 0)
        {
            led2_on();
            app_printf("[LED] on\r\n");
        }
        else if (strcmp(line, "LED OFF") == 0)
        {
            led2_off();
            app_printf("[LED] off\r\n");
        }
        else if (strcmp(line, "HELP") == 0)
        {
            app_printf("commands: GET | RATE <ms> | LED ON | LED OFF | HELP\r\n");
        }
        else if (line[0] != '\0')
        {
            app_printf("[ERR] unknown: %s\r\n", line);
        }

        /* 把最近一条命令显示到 OLED 第 4 行（和显示任务共用互斥锁） */
        if (xSemaphoreTake(oled_mutex, portMAX_DELAY) == pdTRUE)
        {
            char show[20];
            snprintf(show, sizeof(show), "CMD:%.10s", line);
            oled_show_string(0, 6, show, 16);
            xSemaphoreGive(oled_mutex);
        }
    }
}

/* ---------------- 4) 状态监控任务 ---------------- */
static void monitor_task(void *param)
{
    char stats[512];
    uint32_t up_sec = 0;

    (void)param;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(MONITOR_PERIOD_MS));
        up_sec += MONITOR_PERIOD_MS / 1000;

        app_printf("\r\n===== system monitor (up %lus) =====\r\n",
                   (unsigned long)up_sec);
        if (sensor_task_handle)
            app_printf("sensor  stack free: %u words\r\n",
                       (unsigned)uxTaskGetStackHighWaterMark(sensor_task_handle));
        if (cmd_task_handle)
            app_printf("cmd     stack free: %u words\r\n",
                       (unsigned)uxTaskGetStackHighWaterMark(cmd_task_handle));
        if (display_task_handle)
            app_printf("display stack free: %u words\r\n",
                       (unsigned)uxTaskGetStackHighWaterMark(display_task_handle));
        if (monitor_task_handle)
            app_printf("monitor stack free: %u words\r\n",
                       (unsigned)uxTaskGetStackHighWaterMark(monitor_task_handle));
        app_printf("free heap: %u bytes\r\n",
                   (unsigned)xPortGetFreeHeapSize());

        /* 各任务 CPU 占用率统计（需要 TIM4 作为运行时间基准） */
        vTaskGetRunTimeStats(stats);
        app_printf("%s\r\n", stats);

        led2_toggle();
    }
}

/* ---------------- 空闲钩子：低功耗 + CPU 占用率辅助 ---------------- */
void vApplicationIdleHook(void)
{
    __WFI();    /* 空闲时让 CPU 睡眠，等中断唤醒 */
}

/* ---------------- 栈溢出钩子 ---------------- */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    taskDISABLE_INTERRUPTS();
    printf("!!! stack overflow: %s\r\n", pcTaskName);
    for (;;);
}

/* ---------------- 创建任务/队列/定时器 ---------------- */
void app_tasks_start(void)
{
    /* 显示队列：最多 4 条数据 */
    sensor_queue = xQueueCreate(4, sizeof(SensorData_t));

    /* 互斥锁：保护串口打印和 OLED */
    printf_mutex = xSemaphoreCreateMutex();
    oled_mutex   = xSemaphoreCreateMutex();

    /* 软件定时器：周期触发采样 */
    sample_timer = xTimerCreate("sample", pdMS_TO_TICKS(DEFAULT_SAMPLE_MS),
                                pdTRUE, (void *)0, sample_timer_callback);

    xTaskCreate(sensor_task,  "sensor",  SENSOR_STACK_WORDS,  NULL,
                SENSOR_TASK_PRIO,  &sensor_task_handle);
    xTaskCreate(cmd_task,     "cmd",     CMD_STACK_WORDS,     NULL,
                CMD_TASK_PRIO,     &cmd_task_handle);
    xTaskCreate(display_task, "display", DISPLAY_STACK_WORDS, NULL,
                DISPLAY_TASK_PRIO, &display_task_handle);
    xTaskCreate(monitor_task, "monitor", MONITOR_STACK_WORDS, NULL,
                MONITOR_TASK_PRIO, &monitor_task_handle);

    xTimerStart(sample_timer, 0);
    app_printf("app start: 4 tasks created, sample=%lums\r\n",
               (unsigned long)g_sample_period_ms);
}
