/**
 * =============================================================================
 * 文件: logger.h
 * 功能: 分级日志打印（DEBUG / INFO / WARN / ERROR）
 * 作者: [你的名字]
 * 版本: 1.0
 * =============================================================================
 * 
 * 【使用方法】
 *   1. #include "logger.h"
 *   2. 在程序启动时设置日志级别: log_set_level(LOG_LEVEL_INFO)
 *   3. 在代码中调用 LOG_DEBUG() / LOG_INFO() / LOG_ERROR() 等
 * 
 * 【示例代码】
 *   #include "logger.h"
 *   
 *   int main() {
 *       log_set_level(LOG_LEVEL_DEBUG);  // 显示所有日志
 *       
 *       LOG_DEBUG("初始化网卡 %s", "eth0");
 *       LOG_INFO("路由器启动成功，PID=%d", getpid());
 *       LOG_WARN("ARP缓存即将满，当前%d条", count);
 *       LOG_ERROR("套接字创建失败: %s", strerror(errno));
 *       
 *       // 生产环境只显示错误
 *       log_set_level(LOG_LEVEL_ERROR);
 *   }
 * 
 * 【日志级别】
 *   LOG_LEVEL_DEBUG  - 调试信息（开发时使用）
 *   LOG_LEVEL_INFO   - 常规信息（启动/关闭/关键状态）
 *   LOG_LEVEL_WARN   - 警告（可恢复的异常）
 *   LOG_LEVEL_ERROR   - 错误（需要关注的异常）
 *   LOG_LEVEL_NONE   - 关闭所有日志
 * 
 * 【输出格式】
 *   [DEBUG] 初始化网卡 eth0
 *   [INFO] 路由器启动成功，PID=1234
 *   [WARN] ARP缓存即将满，当前250条
 *   [ERROR] 套接字创建失败: Permission denied
 * 
 * 【依赖的头文件】
 *   - stdio.h (printf)
 * 
 * 【注意事项】
 *   - 日志函数是宏定义，不会额外产生函数调用开销
 *   - 日志级别低于设定级别时，不会执行任何打印
 *   - 支持 printf 格式字符串（%d, %s, %x 等）
 * =============================================================================
 */

#ifndef __LOGGER_H__
#define __LOGGER_H__

#include <stdio.h>

/* ==================== 日志级别定义 ==================== */

/** 日志级别：DEBUG（调试信息，最详细） */
#define LOG_LEVEL_DEBUG  0

/** 日志级别：INFO（常规信息，如启动/停止） */
#define LOG_LEVEL_INFO   1

/** 日志级别：WARN（警告，可恢复的异常） */
#define LOG_LEVEL_WARN   2

/** 日志级别：ERROR（错误，需要关注） */
#define LOG_LEVEL_ERROR  3

/** 日志级别：NONE（关闭所有日志） */
#define LOG_LEVEL_NONE   4

/* ==================== 全局变量声明 ==================== */

/**
 * 当前日志级别（全局变量）
 * 默认为 LOG_LEVEL_INFO（只显示 INFO 及以上级别）
 */
extern int g_log_level;

/* ==================== 修改点（新增） ====================
 * 讲解: 日志输出目标（默认 stderr）。屏幕只留给命令交互，
 *       log_init("router.log") 后日志全部写入文件。
 * ========================================================= */
extern FILE *g_log_fp;

/* ==================== 函数声明 ==================== */

/**
 * 初始化日志输出目标（修改点（新增））
 *
 * @param path  日志文件路径；传入 NULL 则保持 stderr（终端）
 *
 * 讲解: 原 logger 直接用 printf 打 stdout，日志与命令提示符挤在同一通道；
 *       调用本函数后日志写入文件，屏幕只用于命令交互。
 */
extern void log_init(const char *path);

/**
 * 输出一条日志（修改点（新增））
 * 讲解: LOG_* 宏统一走此函数，输出到 g_log_fp，并立即 fflush，
 *       保证 tail -f router.log 实时可见。
 */
extern void log_output(const char *fmt, ...);

/**
 * 设置日志级别
 * 
 * @param level  日志级别（LOG_LEVEL_DEBUG / LOG_LEVEL_INFO / ...）
 * 
 * @示例
 *   log_set_level(LOG_LEVEL_DEBUG);  // 开发模式，显示所有
 *   log_set_level(LOG_LEVEL_ERROR);  // 生产模式，只显示错误
 */
extern void log_set_level(int level);

/**
 * 获取当前日志级别
 * 
 * @return  当前日志级别
 */
extern int log_get_level(void);

/* ==================== 日志宏定义 ==================== */

/**
 * DEBUG级别日志（开发调试用）
 * 
 * 使用示例:
 *   LOG_DEBUG("连接目标: %s:%d", "192.168.1.1", 8080);
 */
/* ==================== 修改点（原 → 现） ====================
 * 原: printf("[DEBUG] " fmt "\n", ...)   // 直接打 stdout，与提示符混在一起
 * 现: log_output("[DEBUG] " fmt "\n", ...)  // 走日志目标（文件/stderr）
 * 讲解: 日志与命令交互分离，屏幕保持干净。
 * ========================================================= */
#define LOG_DEBUG(fmt, ...) \
    do { \
        if (g_log_level <= LOG_LEVEL_DEBUG) { \
            log_output("[DEBUG] " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

/**
 * INFO级别日志（关键操作记录）
 * 
 * 使用示例:
 *   LOG_INFO("路由器启动成功");
 *   LOG_INFO("收到ARP请求: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
 */
#define LOG_INFO(fmt, ...) \
    do { \
        if (g_log_level <= LOG_LEVEL_INFO) { \
            log_output("[INFO] " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

/**
 * WARN级别日志（警告，可恢复）
 * 
 * 使用示例:
 *   LOG_WARN("ARP缓存已满，丢弃新条目");
 *   LOG_WARN("校验和错误，重传次数: %d", retry);
 */
#define LOG_WARN(fmt, ...) \
    do { \
        if (g_log_level <= LOG_LEVEL_WARN) { \
            log_output("[WARN] " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

/**
 * ERROR级别日志（错误，需要关注）
 * 
 * 使用示例:
 *   LOG_ERROR("socket创建失败: %s", strerror(errno));
 *   LOG_ERROR("转发超时，丢弃包");
 */
#define LOG_ERROR(fmt, ...) \
    do { \
        if (g_log_level <= LOG_LEVEL_ERROR) { \
            log_output("[ERROR] " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

#endif /* __LOGGER_H__ */