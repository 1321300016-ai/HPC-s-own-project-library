#ifndef __GATE_H__
#define __GATE_H__

#include "stdint.h"

#define GATE_STATUS_ON 	0 //闭合状态
#define GATE_STATUS_OFF 	1 //松开状态

void gate_init(void); //初始化 GPIO 函数
void gate_on(void); //闭合继电器
void gate_off(void); //松开继电器
uint8_t get_gate_status(void); //获取继电器状态

#endif

