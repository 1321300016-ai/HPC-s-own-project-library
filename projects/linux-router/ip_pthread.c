/**
 * ip_pthread.c - IP包转发线程
 * 
 * 这是整个路由器的核心模块！
 * 流程：
 *   1. 提取目的IP
 *   2. 查路由表找出口网卡
 *   3. 查ARP缓存获取目的MAC
 *   4. 没有则发ARP请求（最多3次）
 *   5. 修改数据包的MAC地址
 *   6. 从出口网卡转发出去
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <errno.h>
#include <semaphore.h>   /* 修改点（新增）：转发线程限流信号量 */

#include "ip_pthread.h"
#include "arp_cache.h"
#include "net_iface.h"
#include "logger.h"
#include "static_pool.h"
#include <arpa/inet.h>
#include "main.h"


/* ==================== 外部引用 ==================== */

/* ==================== 修改点（原 → 现） ====================
 * 原: extern int raw_sock_fd;   // 本文件重复声明
 * 现: 删除本文件的 extern（main.h 中已提供 extern 声明）
 * 讲解: main.h 已统一声明 raw_sock_fd，这里再写一遍属于冗余，
 *       容易在修改时产生不一致。
 * ========================================================= */

/** 包内存池（在 main.c 中定义） */
extern STATIC_POOL g_packet_pool;

/** 转发线程限流信号量（在 main.c 中定义）—— 修改点（新增） */
extern sem_t g_thread_sem;


/* ==================== ARP请求包模板 ==================== */

/**
 * 42字节ARP请求包（以太网头 + ARP头）
 * 偏移说明：
 *   [0-5]   目的MAC（广播：FF:FF:FF:FF:FF:FF）
 *   [6-11]  源MAC（待填充）
 *   [12-13] 协议类型（0x0806 = ARP）
 *   [14-15] 硬件类型（0x0001 = Ethernet）
 *   [16-17] 协议类型（0x0800 = IP）
 *   [18]    硬件地址长度（6）
 *   [19]    协议地址长度（4）
 *   [20-21] 操作码（0x0001 = ARP请求）
 *   [22-27] 发送方MAC（待填充）
 *   [28-31] 发送方IP（待填充）
 *   [32-37] 目标MAC（待填充）
 *   [38-41] 目标IP（待填充）
 */
/* ==================== 修改点（原 → 现） ====================
 * 原: static unsigned char arp_buf[42] = {...};  // 全局静态缓冲区
 * 现: 不再使用全局缓冲区，ARP 请求包改为线程局部构造
 * 讲解: 多个转发线程会并发调用 create_arp_request()，若共用全局
 *       arp_buf，线程A填入的 IP/MAC 可能被线程B覆盖，发出错误请求。
 *       每个线程在自己的栈上构造缓冲区即可避免该竞态。
 * ========================================================= */


/* ==================== 私有函数 ==================== */

/**
 * 查找目的IP对应的出口网卡索引
 * 比较IP地址的前3字节（/24网段）
 * @param dst_ip  目的IP地址（4字节）
 * @return        网卡索引，如果找不到返回 g_iface_count
 */
static int find_network_segment(unsigned char *dst_ip)
{
    int i, j;
    
    for (i = 0; i < g_iface_count; i++) {
        /* ==================== 修改点（原 → 现） ====================
         * 原: 比较 IP 前 3 字节，硬编码假设所有网卡都是 /24
         * 现: 按每块网卡自己的 netmask 做网络号比较，并跳过 DOWN 接口
         * 讲解: 网卡可能是 /16、/8 等；只比前 3 字节会误判。
         *       (IP & mask) == (网卡IP & mask) 才是正确的同网段判断。
         * ========================================================= */
        if (!g_ifaces[i].is_up) {
            continue;   /* 跳过 DOWN 的网卡 */
        }
        for (j = 0; j < 4; j++) {
            if ((dst_ip[j] & g_ifaces[i].netmask[j]) !=
                (g_ifaces[i].ip[j] & g_ifaces[i].netmask[j])) {
                break;
            }
        }
        if (j == 4) {
            return i;   /* 4 个字节的网络号都匹配 */
        }
    }
    return g_iface_count;  // 未找到
}

/**
 * 从指定网卡发送数据
 * @param eth_name  网卡名称（如 "ens33"）
 * @param data      要发送的数据
 * @param data_len  数据长度
 * @return          实际发送字节数，-1=失败
 */
static int send_msg(const char *eth_name, unsigned char *data, unsigned long data_len)
{
    struct ifreq ethreq;
    struct sockaddr_ll sll;
    int ret;
    
    /* 获取网卡接口索引 */
    strncpy(ethreq.ifr_name, eth_name, IFNAMSIZ);
    if (ioctl(raw_sock_fd, SIOCGIFINDEX, &ethreq) == -1) {
        LOG_ERROR("ioctl SIOCGIFINDEX 失败: %s", strerror(errno));
        perror("ioctl");
        return -1;
    }
    
    /* 构造套接字地址 */
    memset(&sll, 0, sizeof(sll));
    sll.sll_ifindex = ethreq.ifr_ifindex;
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ALL);
    
    /* 发送数据 */
    ret = sendto(raw_sock_fd, data, data_len, 0, 
                 (struct sockaddr *)&sll, sizeof(sll));
    if (ret < 0) {
        LOG_ERROR("sendto 失败: %s", strerror(errno));
        perror("sendto");
    }
    
    return ret;
}

/**
 * 组建ARP请求包
 * @param dst_ip        目的IP地址
 * @param iface_idx     出口网卡索引
 */
/* ==================== 修改点（原 → 现） ====================
 * 原: static void create_arp_request(unsigned char *dst_ip, int iface_idx)
 *     —— 操作全局 arp_buf
 * 现: static void create_arp_request(unsigned char *arp_buf, ...)
 *     —— 操作调用者传入的线程局部缓冲区
 * 讲解: 见上方全局 arp_buf 的说明；缓冲区由调用线程在栈上提供，
 *       不再跨线程共享。
 * ========================================================= */
static void create_arp_request(unsigned char *arp_buf, unsigned char *dst_ip, int iface_idx)
{
    NET_IFACE *iface = &g_ifaces[iface_idx];
    
    /* 从零开始构造 42 字节 ARP 请求 */
    memset(arp_buf, 0, 42);
    memset(arp_buf, 0xFF, 6);                  /* 目的MAC：广播 */
    arp_buf[12] = 0x08; arp_buf[13] = 0x06;   /* 协议类型：ARP */
    arp_buf[14] = 0x00; arp_buf[15] = 0x01;   /* 硬件类型：Ethernet */
    arp_buf[16] = 0x08; arp_buf[17] = 0x00;   /* 协议类型：IP */
    arp_buf[18] = 0x06;                       /* 硬件地址长度：6 */
    arp_buf[19] = 0x04;                       /* 协议地址长度：4 */
    arp_buf[20] = 0x00; arp_buf[21] = 0x01;   /* 操作码：ARP请求 */
    
    /* 填充源MAC */
    memcpy(arp_buf + 6, iface->mac, 6);
    memcpy(arp_buf + 22, iface->mac, 6);
    
    /* 填充源IP */
    memcpy(arp_buf + 28, iface->ip, 4);
    
    /* 填充目的IP */
    memcpy(arp_buf + 38, dst_ip, 4);
    
    /* 目的MAC保持全0（ARP请求中该字段为0，memset 后已满足） */
}


/* ==================== 线程入口函数 ==================== */

void *ip_pthread(void *arg)
{
    RECV_DATA *msg_data = (RECV_DATA *)arg;
    unsigned char *dst_ip;
    unsigned char mac[6];        /* 修改点：原为指针，现为局部数组（安全拷贝） */
    int iface_idx;
    int ret;
    int retry;
    
    if (msg_data == NULL) {
        return NULL;
    }
    
    /* ==================== 修改点（原 → 现） ====================
     * 原: dst_ip = (unsigned char *)msg_data->data + 30;  // 固定偏移，无校验
     * 现: 先校验长度与 IP 版本，再按 IHL 计算目的 IP 偏移
     * 讲解: 与 main.c 的解析保持一致；IP 头带选项时 IHL 更大，
     *       固定偏移 30 会取错位置。最小长度 34 = 14(帧头)+20(IP头)。
     * ========================================================= */
    if (msg_data->data_len < 34) {
        LOG_DEBUG("转发线程: IP 包长度异常: %zd", msg_data->data_len);   /* 修改：WARN → DEBUG */
        goto IP_PTHREAD_END;
    }
    {
        unsigned char ver_ihl = msg_data->data[14];
        if ((ver_ihl >> 4) != 4) {
            LOG_DEBUG("转发线程: 非 IPv4 包，忽略");
            goto IP_PTHREAD_END;
        }
        int ip_hdr_len = (ver_ihl & 0x0F) * 4;
        dst_ip = (unsigned char *)msg_data->data + 14 + 16 + (ip_hdr_len - 20);
    }
    
    LOG_DEBUG("转发线程: 目的IP=%d.%d.%d.%d, 数据长度=%zd",
              dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3], 
              msg_data->data_len);
    
    /* 2. 查找出口网卡 */
    iface_idx = find_network_segment(dst_ip);
    if (iface_idx == g_iface_count) {
        LOG_DEBUG("无路由: %d.%d.%d.%d",   /* 修改：WARN → DEBUG，无路由是路由器的正常行为 */
                 dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
        goto IP_PTHREAD_END;
    }
    LOG_DEBUG("出口网卡: %s", g_ifaces[iface_idx].name);
    
    /* 3. 检查广播地址（主机位全为1）—— 修改点：按 netmask 判断，不再只看第4字节 */
    {
        int j, is_bcast = 1;
        for (j = 0; j < 4; j++) {
            if ((dst_ip[j] | g_ifaces[iface_idx].netmask[j]) != 0xFF) {
                is_bcast = 0;
                break;
            }
        }
        if (is_bcast) {
            LOG_DEBUG("广播包，不转发");
            goto IP_PTHREAD_END;
        }
    }
    
    /* 4. 检查环回接口 */
    if (strcmp(g_ifaces[iface_idx].name, "lo") == 0) {
        LOG_DEBUG("环回接口，不转发");
        goto IP_PTHREAD_END;
    }
    
    /* 5. 查ARP缓存（修改点：改为安全拷贝接口，结果存入局部 mac[]） */
    if (arp_lookup(dst_ip, mac) != 0) {
        int mac_found = 0;   /* 修改点（新增）：记录重试期间是否命中 */
        
        /* 6. 如果没有MAC，发送ARP请求（最多3次） */
        LOG_DEBUG("ARP缓存未命中,发送ARP请求: %d.%d.%d.%d",
                  dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
        
        for (retry = 0; retry < 3; retry++) {
            unsigned char arp_buf[42];   /* 修改点（新增）：线程局部缓冲区，替代原全局 arp_buf */
            
            /* 组建并发送ARP请求 */
            create_arp_request(arp_buf, dst_ip, iface_idx);
            ret = send_msg(g_ifaces[iface_idx].name, arp_buf, sizeof(arp_buf));
            if (ret < 0) {
                LOG_WARN("发送ARP请求失败 (重试%d)", retry + 1);   /* 修改：ERROR → WARN，有重试机制 */
            }
            
            /* 等待200ms */
            usleep(200000);
            
            /* 重新查询ARP缓存 */
            if (arp_lookup(dst_ip, mac) == 0) {
                mac_found = 1;
                LOG_DEBUG("ARP获取成功: %d.%d.%d.%d -> %02x:%02x:%02x:%02x:%02x:%02x",
                          dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3],
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                break;
            }
        }
        
        /* 3次都失败，放弃转发 */
        if (!mac_found) {
            LOG_DEBUG("ARP超时: %d.%d.%d.%d",   /* 修改：WARN → DEBUG，目标不在线是常态 */
                     dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
            goto IP_PTHREAD_END;
        }
    }
    
    /* 7. 修改数据包的MAC地址（核心操作） */
    /*    目的MAC = 目标设备的MAC */
    memcpy(msg_data->data, mac, 6);
    /*    源MAC = 出口网卡的MAC */
    memcpy(msg_data->data + 6, g_ifaces[iface_idx].mac, 6);
    
    LOG_DEBUG("MAC已修改: 目的MAC=%02x:%02x:%02x:%02x:%02x:%02x",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    /* 8. 转发数据 */
    ret = send_msg(g_ifaces[iface_idx].name, 
                   msg_data->data, msg_data->data_len);
    if (ret < 0) {
        LOG_ERROR("转发失败: %s", strerror(errno));
    } else {
        LOG_DEBUG("转发成功: %d字节 -> %s", ret, g_ifaces[iface_idx].name);
    }

IP_PTHREAD_END:
    /* 9. 释放内存池块（★必须！★） */
    LOG_DEBUG("准备释放内存池块: msg_data=%p", msg_data);   /* 修改：INFO → DEBUG，避免每个包刷屏 */
    pool_free(&g_packet_pool, msg_data);
    /* 10. 归还转发线程限流信号量槽位（与 main.c 的 sem_trywait 对应）—— 修改点（新增） */
    sem_post(&g_thread_sem);
    LOG_DEBUG("转发线程结束，内存池块已释放");
    
    return NULL;
}