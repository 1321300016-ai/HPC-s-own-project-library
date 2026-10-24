#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>   /* 修改点（新增）：黑名单链表互斥锁 */

#include "ip_filter.h"
#include "logger.h"

#define IP_CONFIG_FILE "ip_config"

LIST_HEAD g_ip_filter_head;

unsigned char *g_target_ip = NULL;

/* ==================== 修改点（新增） ====================
 * 讲解: 黑名单链表由主线程（ip_filter_check）与键盘线程
 *       （ip_filter_add/del/show）并发访问，必须加互斥锁；
 *       g_target_ip 的赋值与链表遍历也在同一把锁内，
 *       避免线程间互相覆盖目标 IP。
 * ========================================================= */
pthread_mutex_t g_ip_filter_lock = PTHREAD_MUTEX_INITIALIZER;

/* ==================== 比较和打印函数（给 simple_list 用） ==================== */

/**
 * IP比较函数（符合 list_find 要求：参数是 void*）
 */
static int ip_match(void *data)
{
    IP_DATA *d = (IP_DATA *)data;
    // 外部目标IP通过全局变量传递（因为list_find只传一个参数）
    extern unsigned char *g_target_ip;
    /* ==================== 修改点（原 → 现） ====================
     * 原: return (memcmp(d->ip, g_target_ip, 4) == 0) ? 0 : 1;
     *     —— 若调用方忘记先设置 g_target_ip（值为 NULL），
     *        memcmp 会访问空指针 → 段错误
     * 现: 先判断 g_target_ip 是否为 NULL，为空直接不匹配
     * ========================================================= */
    if (g_target_ip == NULL) {
        return 1;  // 没有目标IP，不匹配
    }
    return (memcmp(d->ip, g_target_ip, 4) == 0) ? 0 : 1;
}

/**
 * IP打印函数（给 list_foreach 用）
 */
static void ip_print(void *data)
{
    IP_DATA *d = (IP_DATA *)data;
    printf("%d.%d.%d.%d\n", d->ip[0], d->ip[1], d->ip[2], d->ip[3]);
}

/**
 * IP释放函数（给 list_destroy 用）
 */
static void ip_free(void *data)
{
    free(data);
}

/* ==================== 公共函数实现 ==================== */

void ip_filter_init(void)
{
    FILE *fp;
    char line[64];
    
    list_init(&g_ip_filter_head);
    
    fp = fopen(IP_CONFIG_FILE, "r");
    if (fp == NULL) {
        LOG_WARN("IP配置文件不存在，跳过加载");
        return;
    }
    
    while (fgets(line, sizeof(line), fp) != NULL) {
        unsigned char ip[4];
        struct in_addr addr;   /* 修改点：原为 unsigned int raw_ip，改为语义明确的 in_addr */
        
        /* 去掉换行符 */
        line[strcspn(line, "\n")] = '\0';
        /* 去掉末尾的\r（Windows换行） */
        line[strcspn(line, "\r")] = '\0';
        
        /* 跳过空行 */
        if (strlen(line) == 0) {
            continue;
        }
        
        if (inet_pton(AF_INET, line, &addr) <= 0) {
            LOG_WARN("无效IP格式: %s", line);
            continue;
        }
        memcpy(ip, &addr, 4);
        
        ip_filter_add(ip);
    }
    
    fclose(fp);
    LOG_INFO("IP黑名单加载完成，共 %d 条", g_ip_filter_head.count);
}

void ip_filter_save(void)
{
    FILE *fp;
    LIST_NODE *p;
    
    fp = fopen(IP_CONFIG_FILE, "w");
    if (fp == NULL) {
        LOG_ERROR("无法打开配置文件写入: %s", strerror(errno));
        return;
    }
    
    /* 修改点（新增）：加锁遍历，避免保存期间链表被其它线程修改 */
    pthread_mutex_lock(&g_ip_filter_lock);
    p = g_ip_filter_head.first;
    while (p != NULL) {
        IP_DATA *d = (IP_DATA *)p->data;
        fprintf(fp, "%d.%d.%d.%d\n", d->ip[0], d->ip[1], d->ip[2], d->ip[3]);
        p = p->next;
    }
    pthread_mutex_unlock(&g_ip_filter_lock);
    
    fclose(fp);
    LOG_INFO("IP黑名单已保存，共 %d 条", g_ip_filter_head.count);
}

int ip_filter_add(unsigned char *ip)
{
    IP_DATA *data;
    LIST_NODE *node;
    
    /* ==================== 修改点（原 → 现） ====================
     * 原: 直接 list_find(&g_ip_filter_head, ip_match);
     *     —— 没有先给 g_target_ip 赋值，ip_match() 里 memcmp 空指针，
     *        只要黑名单非空（配置文件 ≥2 行）就段错误！
     * 现: 先 g_target_ip = ip 绑定目标，再加锁执行查找与插入
     * 讲解: ip_match() 依赖全局 g_target_ip 做比较；加锁保证
     *       g_target_ip 的赋值与链表操作原子，不被其它线程覆盖。
     * ========================================================= */
    pthread_mutex_lock(&g_ip_filter_lock);
    
    g_target_ip = ip;
    node = list_find(&g_ip_filter_head, ip_match);
    if (node != NULL) {
        LOG_DEBUG("IP %d.%d.%d.%d 已在黑名单中", ip[0], ip[1], ip[2], ip[3]);
        pthread_mutex_unlock(&g_ip_filter_lock);   /* 修改点（新增）：失败也要解锁 */
        return -1;
    }
    
    data = (IP_DATA *)malloc(sizeof(IP_DATA));
    if (data == NULL) {
        LOG_ERROR("malloc失败: %s", strerror(errno));
        pthread_mutex_unlock(&g_ip_filter_lock);   /* 修改点（新增）：失败也要解锁 */
        return -1;
    }
    memcpy(data->ip, ip, 4);
    
    list_add_head(&g_ip_filter_head, data);
    LOG_DEBUG("添加IP到黑名单: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    pthread_mutex_unlock(&g_ip_filter_lock);   /* 修改点（新增）：解锁后返回 */
    return 0;
}

int ip_filter_del(unsigned char *ip)
{
    void *deleted;
    
    /* ==================== 修改点（原 → 现） ====================
     * 原: 直接 list_delete(&g_ip_filter_head, ip_match);
     *     —— 与 ip_filter_add 同样的空指针问题，delip 命令一执行就崩
     * 现: 先设置 g_target_ip，再加锁执行删除
     * ========================================================= */
    pthread_mutex_lock(&g_ip_filter_lock);
    
    g_target_ip = ip;
    deleted = list_delete(&g_ip_filter_head, ip_match);
    if (deleted == NULL) {
        LOG_DEBUG("IP %d.%d.%d.%d 不在黑名单中", ip[0], ip[1], ip[2], ip[3]);
        pthread_mutex_unlock(&g_ip_filter_lock);   /* 修改点（新增）：失败也要解锁 */
        return -1;
    }
    
    free(deleted);
    LOG_DEBUG("从黑名单删除IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    pthread_mutex_unlock(&g_ip_filter_lock);   /* 修改点（新增）：解锁后返回 */
    return 0;
}

int ip_filter_check(unsigned char *ip)
{
    LIST_NODE *node;
    
    /* ★★★ 关键：使用全局变量传递目标IP ★★★ */
    extern unsigned char *g_target_ip;
    /* 修改点（新增）：加锁，避免与键盘线程的 add/del 并发访问链表 */
    pthread_mutex_lock(&g_ip_filter_lock);
    g_target_ip = ip;
    node = list_find(&g_ip_filter_head, ip_match);
    pthread_mutex_unlock(&g_ip_filter_lock);
    return (node != NULL) ? 1 : 0;
}

void ip_filter_show(void)
{
    /* 修改点（新增）：加锁遍历，避免与主线程的 check 并发 */
    pthread_mutex_lock(&g_ip_filter_lock);
    
    if (g_ip_filter_head.count == 0) {
        printf("IP黑名单为空\n");
        pthread_mutex_unlock(&g_ip_filter_lock);
        return;
    }
    printf("\n========== IP黑名单 ==========\n");
    list_foreach(&g_ip_filter_head, ip_print);
    printf("总数: %d\n", g_ip_filter_head.count);
    printf("==============================\n\n");
    pthread_mutex_unlock(&g_ip_filter_lock);
}

void ip_filter_destroy(void)
{
    /* 修改点（新增）：加锁后释放整条链表 */
    pthread_mutex_lock(&g_ip_filter_lock);
    list_destroy(&g_ip_filter_head, ip_free);
    pthread_mutex_unlock(&g_ip_filter_lock);
}