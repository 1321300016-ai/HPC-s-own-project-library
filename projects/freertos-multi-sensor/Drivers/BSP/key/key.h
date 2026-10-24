#ifndef __KEY_H__
#define __KEY_H__

#include "sys.h"

//初始化 GPIO
void key_init(void);
//按键扫描函数
uint8_t key_scan(void);

#endif
