#ifndef __EXTI_H__
#define __EXTI_H__

#include "stdint.h"

#define TRUE 1
#define FALSE 0

void exti_init(void); //初始化中断
uint8_t get_ia_flag(void); //获取红外状态
void set_ia_flag(uint8_t value); //设置红外状态

#endif
