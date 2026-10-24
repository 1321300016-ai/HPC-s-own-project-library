#include "FreeRTOS.h"
#include "task.h"
#include "freertos_test.h"
#include "key.h"
#include "led.h"
#include "beep.h"
#include "exti.h"
#include "gate.h"
#include "oled.h"
#include "queue.h"
#include "timers.h"
#include "semphr.h"
#include "string.h"

TaskHandle_t sensor_task_handle; //传感器监控任务句柄
TaskHandle_t device_task_handle; //设备控制任务句柄
TaskHandle_t display_task_handle; //显示任务句柄

QueueHandle_t display_queue; //显示消息队列句柄
QueueHandle_t state_queue; //状态队列句柄

TimerHandle_t wait_timer; //等待定时器句柄

SemaphoreHandle_t oled_mutex; //OLED 互斥锁

SemaphoreHandle_t passage_semaphore; //通行权信号量，最多 5 个

#define NOTIFY_WAIT_TIMEOUT 0x01 //等待超时通知
#define NOTIFY_PERSON_PASS 0x02  //有人通过通知

#define MAX_PASSAGES 5 //最多通行人数

//状态定义
enum
{
	PASS_STATE, //通行状态
	WAIT_STATE  //等待状态
};

//显示消息类型定义
typedef struct
{
	uint8_t row;
	uint8_t col;
	char message[16];
}DisplayMessage_t;

uint8_t system_state = PASS_STATE; //系统状态，一开始是允许通行状态

//传感器监控任务函数
void sensor_monitor_task(void *pvParams)
{
	uint8_t current_state;
	DisplayMessage_t display_msg;
	uint32_t available_passages = MAX_PASSAGES; //可用的通行次数
	uint32_t notification_value;
	
	//初始状态
	system_state = PASS_STATE;
	//发送初始状态给状态队列
	xQueueSend(state_queue, &system_state, 0); //不阻塞
	//初始显示：发送数据到消息显示队列
	sprintf(display_msg.message, "PASS...00/05");
	display_msg.row = 1;
	display_msg.col = 1;
	xQueueSend(display_queue, &display_msg, 0); //不阻塞
	
	while(1)
	{
		//获取当前状态
		current_state = system_state;
		//检测红外传感器
		//允许通行的状态下检查红外传感器，看是否有人通过
		if(current_state == PASS_STATE && get_ia_flag() == TRUE)
		{
			set_ia_flag(FALSE); //清除标志
			//尝试获取一个通信权(计数型信号量)
			if(xSemaphoreTake(passage_semaphore, 0) == pdTRUE) //计数还没用完
			{
				available_passages--; //可用通行次数减 1
				//更新显示：发送数据到消息显示队列
				sprintf(display_msg.message, "PASS...%02d/05", MAX_PASSAGES - available_passages);
				display_msg.row = 1;
				display_msg.col = 1;
				xQueueSend(display_queue, &display_msg, 0); //不阻塞
				//检查是否已无可用通行权
				if(available_passages == 0)
				{
					//如果用完则切换到不允许通行状态
					system_state = WAIT_STATE;
					//发送初始状态给状态队列
					xQueueSend(state_queue, &system_state, 0); //不阻塞
					//更新显示：发送数据到消息显示队列
					sprintf(display_msg.message, "WAIT...00/05");
					display_msg.row = 1;
					display_msg.col = 1;
					xQueueSend(display_queue, &display_msg, 0); //不阻塞
				}
			}
		}
		
		//等待任务通知或立即返回
		//参数 0：进入前不清除标志，UINT32_MAX=0xFFFFFFFF, 读到通知后清空所有通知位，最后一个参数0 不阻塞
		if(xTaskNotifyWait(0, UINT32_MAX, &notification_value, 0) == pdTRUE)
		{
			//处理等待超时通知
			if((notification_value & NOTIFY_WAIT_TIMEOUT) != 0) //等到对应类型通知
			{
				if(current_state == WAIT_STATE) //确实是等待状态
				{
					//重置信号量，恢复 5 个通信权
					available_passages = MAX_PASSAGES;
					//确保信号量为空：不断取出信号量
					while(xSemaphoreTake(passage_semaphore, 0) == pdTRUE);
					
					//重新给出 5 个信号量
					for(int i = 0; i < MAX_PASSAGES; i++)
						xSemaphoreGive(passage_semaphore);
					
					set_ia_flag(FALSE); //红外设置为 FALSE
					//切换状态
					system_state = PASS_STATE;
					xQueueSend(state_queue, &system_state, 0);

					//更新显示：发送数据到消息显示队列
					sprintf(display_msg.message, "PASS...00/05");
					display_msg.row = 1;
					display_msg.col = 1;
					xQueueSend(display_queue, &display_msg, 0); //不阻塞
				}
			}
		}
		vTaskDelay(20);
	}
}

//设备控制任务函数
void device_control_task(void *pvParams)
{
	uint8_t current_state = PASS_STATE; //当前状态，初始值为通行状态
	uint16_t count = 0;
	while(1)
	{
		//从状态队列获取最新状态，不阻塞，不能死等，要不然后面的控制就运行不了
		if(xQueueReceive(state_queue, &current_state, 0) == pdPASS)
		{
			//状态变量如果从允许切换到不允许，启动定时器
			if(current_state == WAIT_STATE)
				xTimerStart(wait_timer, 0); //启动定时器，不阻塞
			else if(current_state == PASS_STATE) //如果是允许通行状态
				xTimerStop(wait_timer, 0); //关闭定时器，不阻塞
		}
		//根据状态控制外设
		if(current_state == PASS_STATE) //通行状态
		{
			if(count >= 100)
			{
				//led1 以 1s 频率闪烁
				led1_toggle();
				//led2 不闪烁
				led2_off();
				//蜂鸣器不响
				beep_off();
				//开门
				gate_off();
				count = 0;
			}
			else 
				count++;
			
		}
		else if(current_state == WAIT_STATE) //等待状态
		{
			if(count >= 20)
			{
				//led1 不闪
				led1_off();
				//led2 以 200ms 频率闪烁
				led2_toggle();
				//蜂鸣器响
				beep_on();
				//关门
				gate_on();
				count = 0;
			}
			else 
				count++;
			
		}
		vTaskDelay(10); //10ms 延时
	}
}

//显示任务函数
void display_task(void *pvParams)
{
	DisplayMessage_t display_msg;
	while(1)
	{
		//从队列接收显示信息
		//参数1：消息队列句柄，参数2：接收消息的内存，参数3：阻塞等待
		if(xQueueReceive(display_queue, &display_msg, portMAX_DELAY) == pdPASS) //接收消息成功
		{
			//获取 OLED 互斥锁
			//参数1：互斥锁句柄，参数2：阻塞等待
			if(xSemaphoreTake(oled_mutex, portMAX_DELAY) == pdTRUE) //获取互斥锁成功
			{
				//显示消息到 OLED
				oled_show_string(display_msg.col, display_msg.row, display_msg.message, 16);
				//释放互斥锁
				xSemaphoreGive(oled_mutex);
			}
		}
	}
}

//定时器回调函数
void wait_timer_callback(TimerHandle_t xTimer)
{
	//使用任务通知直接通知传感器任务
	if(sensor_task_handle != NULL)
		//发送通知值 TIMEOUT 给传感器任务
		xTaskNotify(sensor_task_handle, NOTIFY_WAIT_TIMEOUT, eSetBits);
}

void freertos_test(void)
{
	//创建队列
	//3 个队列项目，每个队列项为 uint8_t 大小
	state_queue = xQueueCreate(3, sizeof(uint8_t));
	//3 个队列项目，每个队列项为 DisplayMessage_t 大小
	display_queue = xQueueCreate(3, sizeof(DisplayMessage_t));
	
	//创建互斥锁
	oled_mutex = xSemaphoreCreateMutex();
	
	//创建计数型信号量(通行权)，初始值为 5，表示最多允许 5 个人通过
	//参数1：最多 5 个人，参数2：初始值也是 5
	passage_semaphore = xSemaphoreCreateCounting(MAX_PASSAGES, MAX_PASSAGES);
	
	//创建软件定时器
	wait_timer = xTimerCreate(
												"wait_timer", //名称
												3000, //定时时长 3s
												pdFALSE, //只定时一次
												(void *)0, //定时器 ID
												wait_timer_callback); //定时器回调函数
	
	xTaskCreate(sensor_monitor_task,	//任务函数
							"sensor_task", //任务名称
							128,     //任务堆栈大小
							NULL,			//传给任务函数的参数
							1,				//任务优先级
							&sensor_task_handle); //任务1句柄
	
	xTaskCreate(device_control_task,	//任务函数
							"task_control", //任务名称
							128,     //任务堆栈大小
							NULL,			//传给任务函数的参数
							1,				//任务优先级
							&device_task_handle); //任务2句柄
	xTaskCreate(display_task,	//任务函数
							"display_task", //任务名称
							128,     //任务堆栈大小
							NULL,			//传给任务函数的参数
							1,				//任务优先级
							&display_task_handle); //任务3句柄
}
