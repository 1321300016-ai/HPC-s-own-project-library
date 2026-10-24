#ifndef __SYS_H__
#define __SYS_H__

#include "stm32f1xx.h" //是所有F1系列的顶层头文件，通过条件编译来包含某个芯片的头文件，定义通用的枚举类型，定义通用的宏定义

//plln：倍频系数
void stm32_clock_init(uint32_t plln);

#endif
