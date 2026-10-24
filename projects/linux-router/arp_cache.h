#ifndef __ARP_CACHE_H__
#define __ARP_CACHE_H__

#include "simple_list.h"
#include <time.h>

#define ARP_TIMEOUT 300

typedef struct 
{
    unsigned char ip[4];
    unsigned char mac[6];
    time_t last_seen;
}ARP_DATA;

extern LIST_HEAD g_arp_head;

/* ==================== 修改点（原 → 现） ====================
 * 原: extern void arp_updata(...);   // 拼写错误，实际函数是 arp_update
 * 现: extern void arp_update(...);   // 修正声明，与实现一致
 * 讲解: 原声明无法为 main.c 中的 arp_update() 提供原型，
 *       编译器只能做隐式声明，参数检查全部失效。
 * ========================================================= */
extern void arp_update(unsigned char *ip, unsigned char *mac);

/* ==================== 修改点（原 → 现） ====================
 * 原: extern unsigned char *arp_lookup(unsigned char *ip);
 *     // 返回链表内部指针，多线程下可能被释放/改写
 * 现: extern int arp_lookup(unsigned char *ip, unsigned char *out_mac);
 *     // 在锁内把 MAC 拷贝到调用者缓冲区，返回 0=命中 / -1=未命中
 * 讲解: 原接口把内部指针暴露给调用者，锁释放后节点可能被
 *       arp_update/arp_cleanup 修改或释放，产生 use-after-free；
 *       改为拷贝返回值后调用者拿到的数据是安全的。
 * ========================================================= */
extern int arp_lookup(unsigned char *ip, unsigned char *out_mac);

extern void arp_cleanup(void);

extern void arp_show();

extern void arp_destroy();

#endif