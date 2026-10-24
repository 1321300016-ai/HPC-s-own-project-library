#ifndef __TIME_H__
#define __TIME_H__

#include "sys.h"

//定时器初始化函数
//要配置 ARR 和 PSC 寄存器，都是 16 位的寄存器
void timer_init(uint16_t arr, uint16_t psc);
	
#endif
