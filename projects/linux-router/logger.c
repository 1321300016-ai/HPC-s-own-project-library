/**
 * =============================================================================
 * 文件: logger.c
 * 功能: 实现 logger.h 中声明的所有函数
 * =============================================================================
 * 
 * 【调用关系】
 *   业务代码
 *        ↓
 *   LOG_DEBUG("...")  → 检查 g_log_level → 打印或不打印
 *   LOG_INFO("...")   → 同上
 *   LOG_WARN("...")   → 同上
 *   LOG_ERROR("...")  → 同上
 *   
 *   log_set_level(LOG_LEVEL_ERROR)
 *        ↓
 *   g_log_level = LOG_LEVEL_ERROR  → 只显示 ERROR 级别
 * 
 * 【默认配置】
 *   g_log_level = LOG_LEVEL_INFO
 *   即: DEBUG 不显示，INFO/WARN/ERROR 显示
 * 
 * 【使用建议】
 *   - 开发阶段: log_set_level(LOG_LEVEL_DEBUG)
 *   - 测试阶段: log_set_level(LOG_LEVEL_INFO)
 *   - 生产环境: log_set_level(LOG_LEVEL_WARN 或 LOG_LEVEL_ERROR)
 * =============================================================================
 */

#include "logger.h"
#include <stdarg.h>   /* 修改点（新增）：log_output 变参 */

/* ==================== 全局变量定义 ==================== */

/** 当前日志级别（修改点：默认 INFO → ERROR，屏幕只留真正的错误） */
int g_log_level = LOG_LEVEL_ERROR;

/* ==================== 修改点（原 → 现） ====================
 * 原: FILE *g_log_fp = stderr;
 *     —— 某些编译器/glibc 中 stderr 不是“常量初始化器”，全局变量
 *        初始化报 error: initializer element is not constant
 * 现: 初始化为 NULL，log_output() 运行时回退到 stderr
 * 讲解: 全局指针的初始化器必须是编译期常量；NULL 一定合法，
 *       stderr 留到运行时取，兼容性最好。
 * ========================================================= */
FILE *g_log_fp = NULL;

/* ==================== 函数实现 ==================== */

/* ==================== 修改点（新增） ====================
 * 原: LOG_* 宏直接 printf 到 stdout，日志与命令提示符混在同一通道
 * 现: 日志统一走 log_output()，输出到 g_log_fp（默认 stderr/文件）
 * 讲解: 屏幕是交互通道不是日志通道；日志写文件后，
 *       "> " 提示符不再被日志打断，排查问题用 tail -f router.log。
 * ========================================================= */
void log_output(const char *fmt, ...)
{
    va_list ap;
    /* 修改点：g_log_fp 为 NULL（未 log_init）时回退到 stderr */
    FILE *fp = (g_log_fp != NULL) ? g_log_fp : stderr;

    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fflush(fp);   /* 立即落盘，保证 tail -f 实时可见 */
}

void log_init(const char *path)
{
    FILE *fp;

    if (path == NULL) {
        return;   /* 保持默认 stderr */
    }
    fp = fopen(path, "a");
    if (fp == NULL) {
        fprintf(stderr, "[ERROR] log_init 打开日志文件失败: %s\n", path);
        return;
    }
    g_log_fp = fp;
}

void log_set_level(int level)
{
    if (level < LOG_LEVEL_DEBUG || level > LOG_LEVEL_NONE) {
        return;
    }
    g_log_level = level;
}

int log_get_level(void)
{
    return g_log_level;
}