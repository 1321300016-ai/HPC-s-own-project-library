/**
 * arp_cache.c - ARP缓存实现
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#include "arp_cache.h"
#include "logger.h"
#include <pthread.h>   /* 修改点（新增）：ARP 链表互斥锁 */

/* ==================== 全局变量 ==================== */

LIST_HEAD g_arp_head;

/* ==================== 修改点（新增） ====================
 * 讲解: ARP 链表由主线程（arp_update）、转发线程（arp_lookup）、
 *       键盘线程（arp_show）并发访问，必须加互斥锁保护；
 *       g_arp_target_ip 的赋值与链表遍历也在同一把锁内，
 *       避免“线程A设目标、线程B覆盖目标”的错配问题。
 * ========================================================= */
pthread_mutex_t g_arp_lock = PTHREAD_MUTEX_INITIALIZER;

/* ★★★ 用于 list_find 传递目标IP的全局变量 ★★★ */
static unsigned char *g_arp_target_ip = NULL;

/* ==================== 内部函数（给 simple_list 用） ==================== */

/**
 * ARP匹配函数：比较IP是否相同
 * 注意：list_find 只传一个参数，目标IP通过全局变量 g_arp_target_ip 传递
 */
static int arp_match(void *data)
{
    ARP_DATA *d = (ARP_DATA *)data;
    if (g_arp_target_ip == NULL) {
        return 1;  // 没有目标IP，不匹配
    }
    return (memcmp(d->ip, g_arp_target_ip, 4) == 0) ? 0 : 1;
}

/**
 * ARP打印函数（给 list_foreach 用）
 */
static void arp_print(void *data)
{
    ARP_DATA *d = (ARP_DATA *)data;
    time_t now = time(NULL);
    printf("%d.%d.%d.%d -> %02x:%02x:%02x:%02x:%02x:%02x  (age: %lds)\n",
           d->ip[0], d->ip[1], d->ip[2], d->ip[3],
           d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5],
           now - d->last_seen);
}

/**
 * ARP释放函数（给 list_destroy 用）
 */
static void arp_free(void *data)
{
    free(data);
}

/* ==================== 公共函数实现 ==================== */

void arp_update(unsigned char *ip, unsigned char *mac)
{
    LIST_NODE *node;
    ARP_DATA *data;
    
    if (ip == NULL || mac == NULL) {
        return;
    }
    
    /* ==================== 修改点（新增） ====================
     * 讲解: 加锁保护 ARP 链表。原代码没有任何同步：主线程 arp_update()
     *       与转发线程 arp_lookup()/arp_cleanup() 并发访问同一链表，
     *       会产生数据竞争甚至崩溃。
     * ========================================================= */
    pthread_mutex_lock(&g_arp_lock);
    
    /* 查找是否已存在 */
    g_arp_target_ip = ip;
    node = list_find(&g_arp_head, arp_match);
    
    if (node != NULL) {
        /* 已存在：更新MAC和时间戳 */
        data = (ARP_DATA *)node->data;
        memcpy(data->mac, mac, 6);
        data->last_seen = time(NULL);
        LOG_DEBUG("更新ARP: %d.%d.%d.%d -> %02x:%02x:%02x:%02x:%02x:%02x",
                  ip[0], ip[1], ip[2], ip[3],
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        pthread_mutex_unlock(&g_arp_lock);   /* 修改点（新增）：解锁后返回 */
        return;
    }
    
    /* 不存在：创建新表项 */
    data = (ARP_DATA *)malloc(sizeof(ARP_DATA));
    if (data == NULL) {
        LOG_ERROR("ARP缓存malloc失败: %s", strerror(errno));
        pthread_mutex_unlock(&g_arp_lock);   /* 修改点（新增）：失败分支也要解锁 */
        return;
    }
    
    memcpy(data->ip, ip, 4);
    memcpy(data->mac, mac, 6);
    data->last_seen = time(NULL);
    
    list_add_head(&g_arp_head, data);
    LOG_DEBUG("新增ARP: %d.%d.%d.%d -> %02x:%02x:%02x:%02x:%02x:%02x",
              ip[0], ip[1], ip[2], ip[3],
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    pthread_mutex_unlock(&g_arp_lock);   /* 修改点（新增）：解锁后返回 */
}

/* ==================== 修改点（原 → 现） ====================
 * 原: unsigned char *arp_lookup(unsigned char *ip) —— 返回链表内部指针
 * 现: int arp_lookup(unsigned char *ip, unsigned char *out_mac)
 *     —— 锁内把 MAC 拷贝到调用者缓冲区，返回 0=命中 / -1=未命中
 * 讲解: 原接口暴露内部指针，锁释放后该节点可能被 arp_update/
 *       arp_cleanup 修改或释放，产生 use-after-free；改为拷贝返回值。
 * ========================================================= */
int arp_lookup(unsigned char *ip, unsigned char *out_mac)
{
    LIST_NODE *node;
    ARP_DATA *data;
    
    if (ip == NULL || out_mac == NULL) {
        return -1;
    }
    
    pthread_mutex_lock(&g_arp_lock);   /* 修改点（新增）：加锁保护链表 */
    g_arp_target_ip = ip;
    node = list_find(&g_arp_head, arp_match);
    
    if (node == NULL) {
        pthread_mutex_unlock(&g_arp_lock);
        return -1;
    }
    
    data = (ARP_DATA *)node->data;
    /* 更新访问时间（LRU思想） */
    data->last_seen = time(NULL);
    memcpy(out_mac, data->mac, 6);     /* 修改点：改为在锁内拷贝 MAC */
    pthread_mutex_unlock(&g_arp_lock);
    
    return 0;
}

void arp_cleanup(void)
{
    LIST_NODE *p, *next;
    ARP_DATA *data;
    time_t now = time(NULL);
    int removed = 0;
    
    /* ==================== 修改点（新增） ====================
     * 讲解: 加锁保护 ARP 链表（原代码无锁，与 arp_update/arp_lookup 并发）。
     * ========================================================= */
    pthread_mutex_lock(&g_arp_lock);
    
    p = g_arp_head.first;
    while (p != NULL) {
        next = p->next;
        data = (ARP_DATA *)p->data;
        
        if (now - data->last_seen > ARP_TIMEOUT) {
            /* 超时，删除该节点 */
            list_delete_node(&g_arp_head, p, arp_free);
            removed++;
        }
        p = next;
    }
    
    if (removed > 0) {
        LOG_DEBUG("ARP清理: 删除了 %d 条超时表项，剩余 %d 条",
                  removed, g_arp_head.count);
    }
    pthread_mutex_unlock(&g_arp_lock);   /* 修改点（新增）：清理结束解锁 */
}

void arp_show(void)
{
    /* ==================== 修改点（新增） ====================
     * 讲解: 加锁保护 ARP 链表（原代码无锁，show 由键盘线程调用，
     *       与主线程/转发线程并发读写同一链表）。
     * ========================================================= */
    pthread_mutex_lock(&g_arp_lock);
    
    if (g_arp_head.count == 0) {
        printf("\nARP缓存为空\n");
        pthread_mutex_unlock(&g_arp_lock);
        return;
    }
    
    printf("\n========== ARP缓存 ==========\n");
    printf("IP              MAC                          Age(s)\n");
    printf("----------------------------------------------\n");
    list_foreach(&g_arp_head, arp_print);
    printf("----------------------------------------------\n");
    printf("总数: %d\n", g_arp_head.count);
    printf("=============================\n\n");
    pthread_mutex_unlock(&g_arp_lock);   /* 修改点（新增）：打印结束解锁 */
}

void arp_destroy(void)
{
    /* ==================== 修改点（新增） ====================
     * 讲解: 加锁后再释放整条链表，避免与其它线程的并发访问冲突。
     * ========================================================= */
    pthread_mutex_lock(&g_arp_lock);
    list_destroy(&g_arp_head, arp_free);
    pthread_mutex_unlock(&g_arp_lock);
}