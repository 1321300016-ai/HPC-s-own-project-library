/**
 * key_pthread.c - 键盘命令处理线程
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "cmd_parser.h"
#include "ip_filter.h"
#include "arp_cache.h"
#include "logger.h"

/* ==================== 命令处理函数（必须在 key_pthread 之前定义） ==================== */

static void help_handler(char *arg)
{
    (void)arg;
    cmd_show_help();
}

static void setip_handler(char *arg)
{
    unsigned char ip[4];
    struct in_addr addr;   /* 修改点：原为 unsigned int raw，改为语义明确的 in_addr */
    
    if (arg == NULL || strlen(arg) == 0) {
        LOG_WARN("用法: setip:192.168.1.100");
        return;
    }
    
    if (inet_pton(AF_INET, arg, &addr) <= 0) {
        LOG_ERROR("无效IP格式: %s", arg);
        return;
    }
    memcpy(ip, &addr, 4);
    
    if (ip[3] == 0 || ip[3] == 255 || ip[0] == 0) {
        LOG_WARN("不能过滤该IP: %s", arg);
        return;
    }
    
    if (ip_filter_add(ip) == 0) {
        LOG_INFO("添加IP成功");
        ip_filter_show();
    }
}

static void delip_handler(char *arg)
{
    unsigned char ip[4];
    struct in_addr addr;   /* 修改点：原为 unsigned int raw，改为语义明确的 in_addr */
    
    if (arg == NULL || strlen(arg) == 0) {
        LOG_WARN("用法: delip:192.168.1.100");
        return;
    }
    
    if (inet_pton(AF_INET, arg, &addr) <= 0) {
        LOG_ERROR("无效IP格式: %s", arg);
        return;
    }
    memcpy(ip, &addr, 4);
    
    if (ip_filter_del(ip) == 0) {
        LOG_INFO("删除IP成功");
        ip_filter_show();
    }
}

static void showip_handler(char *arg)
{
    (void)arg;
    ip_filter_show();
}

static void showarp_handler(char *arg)
{
    (void)arg;
    arp_show();
}

static void saveset_handler(char *arg)
{
    (void)arg;
    ip_filter_save();
    LOG_INFO("黑名单已保存到配置文件");
}

static void exit_handler(char *arg)
{
    (void)arg;
    LOG_INFO("用户主动退出");
    ip_filter_save();
    ip_filter_destroy();
    arp_destroy();
    exit(0);
}

/* ==================== 键盘线程入口 ==================== */

void *key_pthread(void *arg)
{
    char input[256];
    static int registered = 0;   /* 修改点（新增）：命令只需注册一次 */
    
    LOG_INFO("key_pthread: 线程启动成功");
    
    /* ==================== 修改点（原 → 现） ====================
     * 原: 只用 strcmp 硬编码识别 help/exit 两个命令，
     *     setip/delip/showarp/saveset 等 handler 全部是死代码，
     *     黑名单功能完全无法使用
     * 现: 通过 cmd_parser 注册所有 handler，统一走 cmd_execute()
     * 讲解: cmd_parser 模块本来就是为了统一管理命令，原代码绕过了它；
     *       接通后 setip/delip/showip/showarp/saveset 均可使用，
     *       exit 也会走 exit_handler 保存黑名单后再退出。
     * ========================================================= */
    if (!registered) {
        cmd_register("help", help_handler);
        cmd_register("setip", setip_handler);
        cmd_register("delip", delip_handler);
        cmd_register("showip", showip_handler);
        cmd_register("showarp", showarp_handler);
        cmd_register("saveset", saveset_handler);
        cmd_register("exit", exit_handler);
        registered = 1;
    }
    
    while (1) {
        printf("\n> ");
        fflush(stdout);
        
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        
        input[strcspn(input, "\n")] = '\0';
        
        if (strlen(input) == 0) {
            continue;
        }
        
        /* 修改点：原为 strcmp 硬编码，现统一交给 cmd_execute() 分发 */
        if (cmd_execute(input) != 0) {
            LOG_WARN("未知命令: %s（输入 help 查看可用命令）", input);
        }
    }
    
    LOG_INFO("键盘线程退出");
    return NULL;
}