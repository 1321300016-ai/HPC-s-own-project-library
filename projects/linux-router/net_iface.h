/**
 * =============================================================================
 * 文件: net_iface.h
 * 功能: 获取Linux系统本机所有网卡信息（IP、MAC、子网掩码、广播地址、状态）
 * 作者: [你的名字]
 * 版本: 1.0
 * =============================================================================
 * 
 * 【使用方法】
 *   1. #include "net_iface.h"
 *   2. 调用 get_net_interfaces() 获取所有网卡信息
 *   3. 通过全局数组 g_ifaces[] 和 g_iface_count 访问数据
 * 
 * 【示例代码】
 *   #include <stdio.h>
 *   #include "net_iface.h"
 *   
 *   int main() {
 *       if (get_net_interfaces() == 0) {
 *           for (int i = 0; i < g_iface_count; i++) {
 *               printf("网卡: %s, IP: %d.%d.%d.%d\n", 
 *                      g_ifaces[i].name,
 *                      g_ifaces[i].ip[0], g_ifaces[i].ip[1],
 *                      g_ifaces[i].ip[2], g_ifaces[i].ip[3]);
 *           }
 *       }
 *       return 0;
 *   }
 * 
 * 【依赖的头文件】
 *   - sys/socket.h
 *   - sys/ioctl.h
 *   - net/if.h
 *   - netinet/in.h
 *   - arpa/inet.h
 * 
 * 【注意事项】
 *   - 必须在Linux环境下编译运行
 *   - 不需要root权限（ioctl查询网卡信息不需要特权）
 *   - 最多支持16个网卡（MAX_INTERFACES可调整）
 * =============================================================================
 */

#ifndef __NET_IFACE_H__
#define __NET_IFACE_H__

/* 最大支持的网卡数量（可自行调整） */
#define MAX_INTERFACES 16

/**
 * 网卡信息结构体
 */
typedef struct net_iface {
    char name[20];              /**< 网卡名称，如 "ens33", "eth0", "wlan0" */
    unsigned char ip[4];        /**< IP地址，网络字节序（192.168.1.1 → [192,168,1,1]） */
    unsigned char mac[6];       /**< MAC地址，如 [0x00, 0x0c, 0x29, 0xaa, 0xbb, 0xcc] */
    unsigned char netmask[4];   /**< 子网掩码，如 [255,255,255,0] */
    unsigned char br_ip[4];     /**< 广播地址，如 [192,168,1,255] */
    int is_up;                  /**< 网卡状态: 1=UP(激活), 0=DOWN(未激活) */
} NET_IFACE;

/* ==================== 全局变量（外部可直接访问） ==================== */

/** 网卡信息数组，调用 get_net_interfaces() 后填充 */
extern NET_IFACE g_ifaces[MAX_INTERFACES];

/** 实际检测到的网卡数量 */
extern int g_iface_count;

/* ==================== 函数声明 ==================== */

/**
 * 获取本机所有网卡信息
 * 
 * 调用后会填充 g_ifaces[] 数组和 g_iface_count 变量
 * 
 * @return  0 = 成功
 *         -1 = 失败（socket创建失败或ioctl调用失败）
 * 
 * @note 该函数内部会创建临时socket，调用完成后自动关闭
 */
extern int get_net_interfaces(void);

/**
 * 根据IP地址查找对应的网卡索引
 * 
 * @param ip  要查找的IP地址（4字节数组，网络字节序）
 * @return    匹配的网卡在 g_ifaces[] 中的索引 (0 ~ g_iface_count-1)
 *            未找到返回 -1
 * 
 * @示例
 *   unsigned char target_ip[4] = {192, 168, 1, 1};
 *   int idx = find_iface_by_ip(target_ip);
 *   if (idx >= 0) {
 *       printf("找到网卡: %s\n", g_ifaces[idx].name);
 *   }
 */
extern int find_iface_by_ip(unsigned char *ip);

/**
 * 根据网卡名称查找对应的网卡索引
 * 
 * @param name  网卡名称字符串，如 "ens33"
 * @return      匹配的网卡在 g_ifaces[] 中的索引
 *              未找到返回 -1
 * 
 * @示例
 *   int idx = find_iface_by_name("eth0");
 *   if (idx >= 0) {
 *       printf("eth0的IP: %d.%d.%d.%d\n", 
 *              g_ifaces[idx].ip[0], g_ifaces[idx].ip[1],
 *              g_ifaces[idx].ip[2], g_ifaces[idx].ip[3]);
 *   }
 */
extern int find_iface_by_name(const char *name);

#endif /* __NET_IFACE_H__ */