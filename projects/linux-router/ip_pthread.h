/**
 * ip_pthread.h - IP包转发线程
 * 
 * 功能：
 *   1. 查路由（找出口网卡）
 *   2. 查ARP缓存（获取目的MAC）
 *   3. 修改MAC地址
 *   4. 从出口网卡转发
 */

#ifndef __IP_PTHREAD_H__
#define __IP_PTHREAD_H__

extern void *ip_pthread(void *arg);

#endif