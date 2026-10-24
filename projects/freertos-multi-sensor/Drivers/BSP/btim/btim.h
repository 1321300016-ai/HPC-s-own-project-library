#ifndef __BTIM_H__
#define __BTIM_H__

#include "sys.h"

/* FreeRTOS 运行时间统计用的高频计数（TIM4，10kHz） */
extern volatile uint32_t FreeRTOSRunTimeTicks;

void ConfigureTimeForRunTimeStats(void);

#endif
