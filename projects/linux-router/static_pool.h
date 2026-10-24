/**
 * =============================================================================
 * 文件: static_pool.h
 * 功能: 静态内存池（替代 malloc/free，消除内存碎片）
 * 作者: [你的名字]
 * 版本: 1.0
 * =============================================================================
 * 
 * 【为什么需要静态内存池？】
 *   嵌入式系统长期运行下，频繁 malloc/free 会产生内存碎片，
 *   最终导致 malloc 失败。静态内存池预分配一块大内存，
 *   按固定大小块管理，无碎片、分配速度快。
 * 
 * 【使用方法】
 *   1. #include "static_pool.h"
 *   2. STATIC_POOL_DEFINE(my_pool, 64, 128)   // 64个块，每块128字节
 *   3. void *ptr = pool_alloc(&my_pool);      // 分配
 *   4. pool_free(&my_pool, ptr);              // 释放
 * 
 * 【示例代码】
 *   // 定义内存池（全局或静态）
 *   STATIC_POOL_DEFINE(g_packet_pool, 32, 2048);  // 32个包，每包2048字节
 *   
 *   void on_packet_received(void) {
 *       void *buf = pool_alloc(&g_packet_pool);
 *       if (buf == NULL) {
 *           printf("池已满，丢弃包\n");
 *           return;
 *       }
 *       memcpy(buf, recv_data, recv_len);
 *       // ... 处理 ...
 *       pool_free(&g_packet_pool, buf);
 *   }
 * 
 * 【依赖的头文件】
 *   - stdlib.h (NULL)
 *   - string.h (memset)
 * 
 * 【注意事项】
 *   - 块大小在编译时固定，不能动态变化
 *   - 分配的数据块大小不能超过块大小
 *   - 池满时 pool_alloc() 返回 NULL
 *   - 释放时需传入正确的指针（否则行为未定义）
 *   - 线程安全（修改点：结构体内置互斥锁，alloc/free/stats/reset 自动加锁）
 * =============================================================================
 */

#ifndef __STATIC_POOL_H__
#define __STATIC_POOL_H__

#include <stddef.h>
#include <pthread.h>   /* 修改点（新增）：内存池内置互斥锁 */

/**
 * 静态内存池结构体
 */
typedef struct {
    unsigned char *pool;        /**< 指向内存池起始地址 */
    unsigned char *used;        /**< 位图：标记每个块是否被占用 */
    int block_count;            /**< 块数量 */
    int block_size;             /**< 每块大小（字节） */
    pthread_mutex_t lock;       /* 修改点（新增）：保护位图操作的互斥锁 */
} STATIC_POOL;

/* ==================== 宏定义 ==================== */

/**
 * 静态定义内存池（在全局或静态区）
 * 
 * @param name   变量名
 * @param count  块数量
 * @param size   每块大小（字节）
 * 
 * @示例
 *   STATIC_POOL_DEFINE(g_pool, 100, 256);
 *   // 相当于:
 *   static unsigned char g_pool_data[100][256];
 *   static unsigned char g_pool_used[(100+7)/8];
 *   STATIC_POOL g_pool = {g_pool_data, g_pool_used, 100, 256};
 */
#define STATIC_POOL_DEFINE(name, count, size) \
    static unsigned char name##_data[(count) * (size)]; \
    static unsigned char name##_used[((count) + 7) / 8]; \
    STATIC_POOL name = { \
        .pool = name##_data, \
        .used = name##_used, \
        .block_count = (count), \
        .block_size = (size), \
        .lock = PTHREAD_MUTEX_INITIALIZER /* 修改点（新增）：静态初始化互斥锁 */ \
    }

/**
 * 初始化动态分配的内存池（堆上）
 * 
 * @param pool   指向 STATIC_POOL 结构体的指针
 * @param count  块数量
 * @param size   每块大小（字节）
 * @return       0 = 成功, -1 = 失败
 * 
 * @示例
 *   STATIC_POOL *pool = malloc(sizeof(STATIC_POOL));
 *   if (pool_init(pool, 100, 256) == 0) {
 *       // 使用 pool
 *   }
 */
extern int pool_init(STATIC_POOL *pool, int count, int size);

/**
 * 销毁动态分配的内存池（释放pool指向的内存）
 * 
 * @param pool  指向 STATIC_POOL 结构体的指针
 */
extern void pool_destroy(STATIC_POOL *pool);

/* ==================== 函数声明 ==================== */

/**
 * 从内存池分配一块内存
 * 
 * @param pool  指向 STATIC_POOL 结构体的指针
 * @return      指向分配的内存块的指针
 *              返回 NULL 表示池已满或参数无效
 * 
 * @note 分配时间复杂度 O(n)，n=块数（修改点：原注释写 O(1)，实际需遍历位图）
 */
extern void *pool_alloc(STATIC_POOL *pool);

/**
 * 释放内存块回内存池
 * 
 * @param pool  指向 STATIC_POOL 结构体的指针
 * @param ptr   指向要释放的内存块（必须是由 pool_alloc 返回的指针）
 * 
 * @note 如果 ptr 为 NULL，函数不做任何事
 * @note 释放时间复杂度 O(1)
 */
extern void pool_free(STATIC_POOL *pool, void *ptr);

/**
 * 获取内存池使用情况
 * 
 * @param pool      指向 STATIC_POOL 结构体的指针
 * @param used      输出：已使用块数（可为NULL）
 * @param total     输出：总块数（可为NULL）
 * @param free      输出：空闲块数（可为NULL）
 * 
 * @示例
 *   int used, total, free;
 *   pool_stats(&g_pool, &used, &total, &free);
 *   printf("使用率: %d/%d (剩余%d)\n", used, total, free);
 */
extern void pool_stats(STATIC_POOL *pool, int *used, int *total, int *free);

/**
 * 重置内存池（释放所有块）
 * 
 * @param pool  指向 STATIC_POOL 结构体的指针
 * 
 * @note 调用后所有块变为空闲状态，相当于全新池
 */
extern void pool_reset(STATIC_POOL *pool);

#endif /* __STATIC_POOL_H__ */