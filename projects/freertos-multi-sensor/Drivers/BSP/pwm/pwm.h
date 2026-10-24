#ifndef __PWM_H__
#define __PWM_H__

#include "sys.h"

//初始化函数
void pwm_init(uint16_t arr, uint16_t psc);
//修改 CCR 寄存器值的函数
void set_pwm_compare(uint16_t val);

#endif
