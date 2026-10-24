/**
 * =============================================================================
 * 文件: cmd_parser.h
 * 功能: 命令行解析器，支持 "cmd:arg" 格式的命令注册和执行
 * 作者: [你的名字]
 * 版本: 1.0
 * =============================================================================
 * 
 * 【使用方法】
 *   1. #include "cmd_parser.h"
 *   2. 在程序启动时调用 cmd_register() 注册所有命令
 *   3. 在键盘/串口输入循环中调用 cmd_execute() 执行命令
 * 
 * 【示例代码——注册命令】
 *   // 定义命令处理函数
 *   void setip_handler(char *arg) {
 *       printf("设置IP: %s\n", arg);
 *   }
 *   void delip_handler(char *arg) {
 *       printf("删除IP: %s\n", arg);
 *   }
 *   void help_handler(char *arg) {
 *       cmd_show_help();
 *   }
 *   
 *   int main() {
 *       // 注册命令
 *       cmd_register("setip", setip_handler);
 *       cmd_register("delip", delip_handler);
 *       cmd_register("help", help_handler);
 *       
 *       // 交互循环
 *       while (1) {
 *           char input[100];
 *           fgets(input, sizeof(input), stdin);
 *           input[strlen(input)-1] = '\0';  // 去掉换行
 *           if (cmd_execute(input) != 0) {
 *               printf("未知命令，输入 help 查看帮助\n");
 *           }
 *       }
 *   }
 * 
 * 【命令格式】
 *   输入: "setip:192.168.1.100"
 *   cmd = "setip", arg = "192.168.1.100"
 * 
 *   输入: "help"  (没有冒号)
 *   cmd = "help", arg = ""
 * 
 * 【依赖的头文件】
 *   - stdio.h (sscanf)
 *   - string.h (strcmp, strcpy)
 * 
 * 【注意事项】
 *   - 最多支持32个命令（MAX_CMD可调整）
 *   - 命令名区分大小写
 *   - 命令名长度不能超过31字符
 *   - 参数长度不能超过127字符
 * =============================================================================
 */

#ifndef __CMD_PARSER_H__
#define __CMD_PARSER_H__

/**
 * 命令回调函数类型
 * 
 * @param arg  命令后面的参数字符串
 *             例如 "setip:192.168.1.1" → arg = "192.168.1.1"
 *             例如 "help" → arg = ""
 */
typedef void (*cmd_handler_t)(char *arg);

/* ==================== 函数声明 ==================== */

/**
 * 注册一个命令
 * 
 * 将命令名和处理函数绑定，存入命令表
 * 
 * @param cmd      命令名称（如 "setip"，不能包含 ':' 字符）
 * @param handler  处理函数指针
 * @return         0 = 成功
 *                -1 = 失败（命令表已满 或 cmd 包含 ':'）
 * 
 * @note 同名命令会覆盖之前的注册（不会报错）
 */
extern int cmd_register(const char *cmd, cmd_handler_t handler);

/**
 * 执行命令
 * 
 * 解析输入字符串，查找匹配的命令并调用对应的处理函数
 * 
 * @param input  输入字符串，格式为 "cmd:arg"
 * @return       0 = 成功（找到命令并执行）
 *              -1 = 失败（找不到匹配的命令）
 * 
 * @note 如果输入字符串包含 ':'，则 ':' 之前为命令名，之后为参数
 *       如果不包含 ':'，则整个字符串为命令名，参数为空
 */
extern int cmd_execute(char *input);

/**
 * 打印所有已注册的命令列表
 * 
 * 用于实现 "help" 命令
 * 
 * @note 输出格式:
 *   ============= Available Commands =============
 *     setip
 *     delip
 *     help
 *   ==============================================
 */
extern void cmd_show_help(void);

#endif /* __CMD_PARSER_H__ */