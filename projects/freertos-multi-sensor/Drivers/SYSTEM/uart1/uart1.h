#ifndef __UART1_H__
#define __UART1_H__

#include "stdio.h"
#include "sys.h"
#include "FreeRTOS.h"
#include "queue.h"

/* 串口接收缓冲区大小（DMA 循环搬运） */
#define UART1_RX_BUF_SIZE       128
/* 一帧命令的最大长度（用于往队列里送） */
#define UART1_FRAME_MAX         64

/* 一帧不定长数据 */
typedef struct
{
    uint8_t  data[UART1_FRAME_MAX];
    uint16_t len;
} UartFrame_t;

void uart1_init(uint32_t baudrate);

/* 任务里等待并取走一帧数据；timeout 用 portMAX_DELAY 表示一直等 */
BaseType_t uart1_wait_frame(UartFrame_t *frame, TickType_t timeout);

#endif
