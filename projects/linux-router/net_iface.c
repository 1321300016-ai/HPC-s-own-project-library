/**
 * =============================================================================
 * 文件: net_iface.c
 * 功能: 实现 net_iface.h 中声明的所有函数
 * =============================================================================
 * 
 * 【内部实现原理】
 *   1. socket(AF_INET, SOCK_DGRAM, 0) 创建临时套接字
 *   2. ioctl(SIOCGIFCONF) 获取所有网卡列表
 *   3. 对每个网卡分别调用 ioctl 获取:
 *      - SIOCGIFADDR    → IP地址
 *      - SIOCGIFHWADDR  → MAC地址
 *      - SIOCGIFNETMASK → 子网掩码
 *      - SIOCGIFBRDADDR → 广播地址
 *      - SIOCGIFFLAGS   → 状态(UP/DOWN)
 *   4. 所有数据存入 g_ifaces[] 全局数组
 * 
 * 【调用关系】
 *   main.c / 其他业务代码
 *        ↓
 *   get_net_interfaces()
 *        ↓
 *   ┌────────────────────┐
 *   │ socket()           │  创建临时套接字
 *   │ ioctl(SIOCGIFCONF) │  获取网卡列表
 *   │ for each iface:    │
 *   │   ioctl(ADDR)      │  获取IP
 *   │   ioctl(HWADDR)    │  获取MAC
 *   │   ioctl(NETMASK)   │  获取掩码
 *   │   ioctl(BRDADDR)   │  获取广播地址
 *   │   ioctl(FLAGS)     │  获取状态
 *   │ close(sock)        │
 *   └────────────────────┘
 *        ↓
 *   填充 g_ifaces[] 和 g_iface_count
 * 
 * 【注意事项】
 *   - 如果某个网卡没有IP地址（如未配置），对应的ip数组为全0
 *   - 返回的IP/掩码/广播地址均为网络字节序（大端），与直接打印不符
 *     如需打印，应使用 inet_ntoa() 转换，或逐字节打印
 * =============================================================================
 */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if_arp.h>
#include <unistd.h>

#include "net_iface.h"

/* ==================== 全局变量定义 ==================== */

/** 网卡信息数组 */
NET_IFACE g_ifaces[MAX_INTERFACES];

/** 实际检测到的网卡数量 */
int g_iface_count = 0;

/* ==================== 函数实现 ==================== */

int get_net_interfaces(void)
{
    int sock_fd;
    struct ifreq ifr_buf[MAX_INTERFACES];
    struct ifconf ifc;
    int i;
    
    /* 1. 创建临时套接字（用于ioctl调用） */
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        perror("get_net_interfaces: socket");
        return -1;
    }
    
    /* 2. 获取所有网卡列表 */
    ifc.ifc_len = sizeof(ifr_buf);
    ifc.ifc_buf = (caddr_t)ifr_buf;
    
    if (ioctl(sock_fd, SIOCGIFCONF, &ifc) < 0) {
        perror("get_net_interfaces: SIOCGIFCONF");
        close(sock_fd);
        return -1;
    }
    
    /* 3. 计算实际网卡数量，防止溢出 */
    g_iface_count = ifc.ifc_len / sizeof(struct ifreq);
    if (g_iface_count > MAX_INTERFACES) {
        g_iface_count = MAX_INTERFACES;
    }
    
    /* 4. 逐个获取每个网卡的详细信息 */
    for (i = 0; i < g_iface_count; i++) {
        /* 4.1 复制网卡名称 */
        strcpy(g_ifaces[i].name, ifr_buf[i].ifr_name);
        
        /* 4.2 获取IP地址 */
        if (ioctl(sock_fd, SIOCGIFADDR, &ifr_buf[i]) == 0) {
            struct sockaddr_in *addr = (struct sockaddr_in *)&ifr_buf[i].ifr_addr;
            memcpy(g_ifaces[i].ip, &addr->sin_addr, 4);
        } else {
            /* 获取失败则置为0 */
            memset(g_ifaces[i].ip, 0, 4);
        }
        
        /* 4.3 获取MAC地址 */
        if (ioctl(sock_fd, SIOCGIFHWADDR, &ifr_buf[i]) == 0) {
            memcpy(g_ifaces[i].mac, ifr_buf[i].ifr_hwaddr.sa_data, 6);
        } else {
            memset(g_ifaces[i].mac, 0, 6);
        }
        
        /* 4.4 获取子网掩码 */
        if (ioctl(sock_fd, SIOCGIFNETMASK, &ifr_buf[i]) == 0) {
            struct sockaddr_in *addr = (struct sockaddr_in *)&ifr_buf[i].ifr_addr;
            memcpy(g_ifaces[i].netmask, &addr->sin_addr, 4);
        } else {
            memset(g_ifaces[i].netmask, 0, 4);
        }
        
        /* 4.5 获取广播地址 */
        if (ioctl(sock_fd, SIOCGIFBRDADDR, &ifr_buf[i]) == 0) {
            struct sockaddr_in *addr = (struct sockaddr_in *)&ifr_buf[i].ifr_addr;
            memcpy(g_ifaces[i].br_ip, &addr->sin_addr, 4);
        } else {
            memset(g_ifaces[i].br_ip, 0, 4);
        }
        
        /* 4.6 获取网卡状态 (UP/DOWN) */
        if (ioctl(sock_fd, SIOCGIFFLAGS, &ifr_buf[i]) == 0) {
            g_ifaces[i].is_up = (ifr_buf[i].ifr_flags & IFF_UP) ? 1 : 0;
        } else {
            g_ifaces[i].is_up = 0;
        }
    }
    
    close(sock_fd);
    return 0;
}

int find_iface_by_ip(unsigned char *ip)
{
    int i;
    for (i = 0; i < g_iface_count; i++) {
        if (memcmp(g_ifaces[i].ip, ip, 4) == 0) {
            return i;
        }
    }
    return -1;
}

int find_iface_by_name(const char *name)
{
    int i;
    for (i = 0; i < g_iface_count; i++) {
        if (strcmp(g_ifaces[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}