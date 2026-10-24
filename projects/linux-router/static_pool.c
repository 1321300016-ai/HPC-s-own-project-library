/**
 * =============================================================================
 * 文件: static_pool.c
 * 功能: 实现 static_pool.h 中声明的所有函数
 * =============================================================================
 * 
 * 【位图管理说明】
 *   used 数组是位图，每个比特表示一个块的占用状态：
 *   - bit=1: 已占用
 *   - bit=0: 空闲
 *   
 *   例如: used[0] 的 bit0 表示第0个块, bit1 表示第1个块 ...
 *   定位第 n 个块: byte = n/8, bit = n%8
 * 
 * 【分配算法】
 *   1. 遍历位图，找到第一个为0的bit
 *   2. 将其置1
 *   3. 返回该块的内存地址: pool + block_index * block_size
 * 
 * 【释放算法】
 *   1. 计算指针偏移: (ptr - pool) / block_size
 *   2. 将对应的bit置0
 * 
 * 【调用关系】
 *   业务代码
 *        ↓
 *   pool_alloc(pool)
 *        ↓
 *   遍历位图 → 找空闲块 → 置位 → 返回地址
 *   
 *   业务代码
 *        ↓
 *   pool_free(pool, ptr)
 *        ↓
 *   计算块索引 → 清除位 → 完成
 * 
 * 【线程安全】
 *   修改点：结构体内置 pthread_mutex_t 互斥锁，
 *   pool_alloc/pool_free/pool_stats/pool_reset 内部自动加锁，
 *   可直接在多线程环境中使用（原实现无锁，并发分配/释放会破坏位图）。
 * =============================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>   /* 修改点（新增）：内存池互斥锁 */
#include "static_pool.h"
#include "logger.h"    /* 修改点（新增）：用日志宏替代裸 printf */

/* ==================== 私有辅助函数 ==================== */

/**
 * 将位图全置为0（全部空闲）
 */
static void pool_clear_used(STATIC_POOL *pool)
{
    int bytes = (pool->block_count + 7) / 8;
    memset(pool->used, 0, bytes);
}

/**
 * 检查位图某一位是否为1
 */
static int pool_is_used(STATIC_POOL *pool, int index)
{
    int byte = index / 8;
    int bit = index % 8;
    return (pool->used[byte] & (1 << bit)) ? 1 : 0;
}

/**
 * 设置位图某一位为1（标记已占用）
 */
static void pool_set_used(STATIC_POOL *pool, int index)
{
    int byte = index / 8;
    int bit = index % 8;
    pool->used[byte] |= (1 << bit);
}

/**
 * 设置位图某一位为0（标记空闲）
 */
static void pool_clear_used_bit(STATIC_POOL *pool, int index)
{
    int byte = index / 8;
    int bit = index % 8;
    pool->used[byte] &= ~(1 << bit);
}

/* ==================== 公共函数实现 ==================== */

int pool_init(STATIC_POOL *pool, int count, int size)
{
    if (pool == NULL || count <= 0 || size <= 0) {
        return -1;
    }
    
    /* 在堆上分配内存池 */
    pool->pool = (unsigned char *)malloc(count * size);
    if (pool->pool == NULL) {
        return -1;
    }
    
    /* 分配位图 */
    int used_bytes = (count + 7) / 8;
    pool->used = (unsigned char *)malloc(used_bytes);
    if (pool->used == NULL) {
        free(pool->pool);
        return -1;
    }
    
    pool->block_count = count;
    pool->block_size = size;
    
    /* 修改点（新增）：初始化互斥锁 */
    pthread_mutex_init(&pool->lock, NULL);
    
    /* 初始全部空闲 */
    pool_clear_used(pool);
    
    return 0;
}

void pool_destroy(STATIC_POOL *pool)
{
    if (pool == NULL) {
        return;
    }
    /* 修改点（新增）：销毁互斥锁（须在释放资源前） */
    pthread_mutex_destroy(&pool->lock);
    if (pool->pool != NULL) {
        free(pool->pool);
        pool->pool = NULL;
    }
    if (pool->used != NULL) {
        free(pool->used);
        pool->used = NULL;
    }
    pool->block_count = 0;
    pool->block_size = 0;
}

void *pool_alloc(STATIC_POOL *pool)
{
    int i;
    
    if (pool == NULL || pool->pool == NULL) {
        return NULL;
    }
    
    /* ==================== 修改点（原 → 现） ====================
     * 原: 直接遍历位图分配，无锁。
     *     主线程 pool_alloc() 与转发线程 pool_free() 并发时，
     *     “查位→置位”两步不是原子的，可能同一块被分配两次
     * 现: 加互斥锁保护位图操作
     * ========================================================= */
    pthread_mutex_lock(&pool->lock);
    
    /* 遍历位图找空闲块 */
    for (i = 0; i < pool->block_count; i++) {
        if (!pool_is_used(pool, i)) {
            pool_set_used(pool, i);
            pthread_mutex_unlock(&pool->lock);
            return pool->pool + i * pool->block_size;
        }
    }
    
    pthread_mutex_unlock(&pool->lock);
    return NULL;  /* 池已满 */
}

void pool_free(STATIC_POOL *pool, void *ptr)
{
    int index;
    unsigned char *p = (unsigned char *)ptr;
    
    if (pool == NULL || ptr == NULL || pool->pool == NULL) {
        LOG_DEBUG("pool_free: 无效参数");   /* 修改：printf → 日志宏 */
        return;
    }
    
    /* 修改点（新增）：加锁，避免与主线程 pool_alloc 并发破坏位图 */
    pthread_mutex_lock(&pool->lock);
    
    if (p < pool->pool || p >= pool->pool + pool->block_count * pool->block_size) {
        LOG_DEBUG("pool_free: ptr 不在池范围内");   /* 修改：printf → 日志宏 */
        pthread_mutex_unlock(&pool->lock);
        return;
    }
    
    index = (p - pool->pool) / pool->block_size;
    
    if (index < 0 || index >= pool->block_count) {
        LOG_DEBUG("pool_free: index 越界");   /* 修改：printf → 日志宏 */
        pthread_mutex_unlock(&pool->lock);
        return;
    }
    
    pool_clear_used_bit(pool, index);
    pthread_mutex_unlock(&pool->lock);   /* 修改点（新增）：解锁 */
}

void pool_stats(STATIC_POOL *pool, int *used, int *total, int *free)
{
    int i;
    int used_count = 0;
    
    if (pool == NULL) {
        if (used) *used = 0;
        if (total) *total = 0;
        if (free) *free = 0;
        return;
    }
    
    /* 修改点（新增）：加锁遍历位图 */
    pthread_mutex_lock(&pool->lock);
    
    for (i = 0; i < pool->block_count; i++) {
        if (pool_is_used(pool, i)) {
            used_count++;
        }
    }
    
    if (used) *used = used_count;
    if (total) *total = pool->block_count;
    if (free) *free = pool->block_count - used_count;
    
    pthread_mutex_unlock(&pool->lock);
}

void pool_reset(STATIC_POOL *pool)
{
    if (pool == NULL || pool->used == NULL) {
        return;
    }
    /* 修改点（新增）：加锁后清空位图 */
    pthread_mutex_lock(&pool->lock);
    pool_clear_used(pool);
    pthread_mutex_unlock(&pool->lock);
}