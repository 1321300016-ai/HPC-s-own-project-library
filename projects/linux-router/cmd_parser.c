/**
 * =============================================================================
 * 文件: cmd_parser.c
 * 功能: 实现 cmd_parser.h 中声明的所有函数
 * =============================================================================
 * 
 * 【内部数据结构】
 *   static CMD_ENTRY cmd_table[MAX_CMD]
 *   数组存储所有已注册的命令
 * 
 * 【调用关系】
 *   主程序初始化
 *        ↓
 *   cmd_register("setip", handler)  → 填充 cmd_table[]
 *   
 *   主程序键盘循环
 *        ↓
 *   cmd_execute("setip:192.168.1.1")
 *        ↓
 *   sscanf() 拆分为 cmd="setip", arg="192.168.1.1"
 *        ↓
 *   遍历 cmd_table[] 查找匹配
 *        ↓
 *   调用 handler(arg)
 * 
 *   cmd_show_help()
 *        ↓
 *   遍历 cmd_table[] 打印所有命令名
 * 
 * 【安全说明】
 *   - 命令名和参数都有长度限制，防止溢出
 * =============================================================================
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cmd_parser.h"

/* ==================== 内部常量 ==================== */

/** 最大支持的命令数量 */
#define MAX_CMD 64

/** 命令名最大长度（含结尾'\0'） */
#define CMD_NAME_LEN 64

/** 参数最大长度（含结尾'\0'） */
#define ARG_LEN 128

/* ==================== 内部数据结构 ==================== */

/**
 * 命令条目（存储命令名和处理函数的映射）
 */
typedef struct {
    char name[CMD_NAME_LEN];    /**< 命令名称 */
    cmd_handler_t handler;      /**< 处理函数指针 */
} CMD_ENTRY;

/* ==================== 内部全局变量 ==================== */

/** 命令表 */
static CMD_ENTRY cmd_table[MAX_CMD];

/** 当前已注册的命令数量 */
static int cmd_count = 0;

/* ==================== 函数实现 ==================== */

int cmd_register(const char *cmd, cmd_handler_t handler)
{
    int i;
    
    /* 检查参数有效性 */
    if (cmd == NULL || handler == NULL) {
        return -1;
    }
    
    /* 检查命令名是否包含 ':'（非法字符） */
    if (strchr(cmd, ':') != NULL) {
        return -1;
    }
    
    /* 检查是否已存在同名命令，如果存在则覆盖 */
    for (i = 0; i < cmd_count; i++) {
        if (strcmp(cmd_table[i].name, cmd) == 0) {
            cmd_table[i].handler = handler;
            return 0;
        }
    }
    
    /* 新增命令 */
    if (cmd_count >= MAX_CMD) {
        return -1;
    }
    
    /* ==================== 修改点（原 → 现） ====================
     * 原: strcpy(cmd_table[cmd_count].name, cmd);
     *     —— 命令名长度无校验，超长输入会写穿 name[64]
     * 现: 先检查长度，超长命令名直接拒绝
     * 讲解: 命令名来自用户输入，必须限制长度防止缓冲区溢出。
     * ========================================================= */
    if (strlen(cmd) >= CMD_NAME_LEN) {
        return -1;   /* 命令名过长 */
    }
    strcpy(cmd_table[cmd_count].name, cmd);
    cmd_table[cmd_count].handler = handler;
    cmd_count++;
    return 0;
}

int cmd_execute(char *input)
{
    char cmd[CMD_NAME_LEN] = "";
    char arg[ARG_LEN] = "";
    int i;
    
    if (input == NULL || input[0] == '\0') {
        return -1;
    }
    
    /* ==================== 修改点（原 → 现） ====================
     * 原: sscanf(input, "%[^:]:%s", cmd, arg);
     *     —— %[^:] 与 %s 都没有宽度限制，超长输入写穿 cmd/arg 缓冲区
     *     strcpy(cmd, input); 同样无长度限制
     *     且每次执行都打印 3 行调试信息，实际运行时刷屏
     * 现: 加最大宽度（63/127），用 snprintf 截断兜底，删除调试打印
     * 讲解: 键盘输入不可信，格式化解析必须限制写入长度。
     * ========================================================= */
    sscanf(input, "%63[^:]:%127s", cmd, arg);
    if (cmd[0] == '\0') {
        snprintf(cmd, sizeof(cmd), "%s", input);   /* 修改：原为 strcpy，防溢出 */
    }
    
    for (i = 0; i < cmd_count; i++) {
        if (strcmp(cmd, cmd_table[i].name) == 0) {
            if (cmd_table[i].handler == NULL) {
                return -1;
            }
            cmd_table[i].handler(arg);
            return 0;
        }
    }
    
    return -1;
}

void cmd_show_help(void)
{
    int i;
    
    printf("============= Available Commands =============\n");
    for (i = 0; i < cmd_count; i++) {
        printf("  %s\n", cmd_table[i].name);
    }
    printf("==============================================\n");
}