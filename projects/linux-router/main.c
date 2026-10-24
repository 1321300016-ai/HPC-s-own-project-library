#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h> //htons
#include <netinet/ether.h> //ETH_P_ALL
#include <pthread.h> //线程
/* ==================== 修改点（新增头文件） ====================
 * 讲解: 新增 <stdint.h>（uint16_t）与 <semaphore.h>（转发线程限流信号量）。
 * ========================================================= */
#include <stdint.h>
#include <semaphore.h>
#include <errno.h>                 /* 修改点（新增）：strerror(errno) */
#include <netpacket/packet.h>      /* 修改点（新增）：SOL_PACKET / sockaddr_ll */

/* ==================== 修改点（原 → 现） ====================
 * 原: 同时包含 <linux/if_packet.h> 与 <netpacket/packet.h>
 *     —— 两个头文件都定义 struct sockaddr_ll / packet_mreq，编译冲突
 * 现: 只保留 <netpacket/packet.h>；PACKET_IGNORE_OUTGOING 在旧头文件
 *     中可能未定义，这里补一个条件定义（内核常量值 23）
 * 讲解: 旧系统头文件没有该宏时 setsockopt 会失败并降级为 WARN，
 *       自环防护仍由“源 MAC 过滤”兜底完成。
 * ========================================================= */
#ifndef PACKET_IGNORE_OUTGOING
#define PACKET_IGNORE_OUTGOING 23
#endif

#include "main.h"
#include "net_iface.h"
#include "logger.h"
#include "static_pool.h"
#include "key_pthread.h"
#include "ip_pthread.h"
#include "arp_cache.h"
#include "ip_filter.h"

STATIC_POOL_DEFINE(g_packet_pool, PACKET_POOL_COUNT, PACKET_POOL_SIZE);

/* ==================== 修改点（原 → 现） ====================
 * 原: main.h 中写 “int raw_sock_fd;”（每个包含 main.h 的 .c 都定义一份）
 * 现: 定义移到 main.c 唯一一处，main.h 中只留 extern 声明
 * 讲解: 全局变量应当在唯一编译单元中定义，头文件只做声明；
 *       否则依赖 GCC -fcommon 的“宽容”行为，换编译选项即链接失败。
 * ========================================================= */
int raw_sock_fd;

/* ==================== 修改点（新增） ====================
 * 讲解: 用信号量限制并发转发线程数。原代码每个 IP 包都创建并 detach
 *       一个线程且不检查返回值，流量大时线程数无上限（线程风暴）。
 *       达到上限时直接丢包，保证进程稳定。
 * ========================================================= */
sem_t g_thread_sem;
#define MAX_IP_THREADS 16
/* ==================== 修改点（新增） ====================
 * 讲解: arp_cleanup() 原来从未被调用，ARP 表只增不减、无上限；
 *       主循环每收到 ARP_CLEANUP_INTERVAL 个包就清理一次超时表项。
 * ========================================================= */
#define ARP_CLEANUP_INTERVAL 512

int main(int argc, char const *argv[])
{
    /* ==================== 修改点（新增） ====================
     * 讲解: 日志写入文件，屏幕只用于命令交互：
     *       log_init("router.log") 之后所有 LOG_* 输出到文件，
     *       "> " 提示符和命令结果仍在屏幕上。
     * ========================================================= */
    log_init("router.log");

    LOG_INFO("路由器已启动");
    
    //网络信息准备工作
    ip_filter_init(); //IP 过滤链表

    /* ==================== 修改点（原 → 现） ====================
     * 原: get_net_interfaces();                  // 第 1 次调用，返回值未检查
     *     if (get_net_interfaces() != 0) { ... } // 第 2 次调用，重复枚举
     * 现: if (get_net_interfaces() != 0) { ... } // 只调用一次并检查
     * 讲解: 原代码重复枚举网卡，第 1 次结果被直接丢弃；单次调用即可。
     * ========================================================= */
    if (get_net_interfaces() != 0) {
        LOG_ERROR("获取网卡信息失败");
        exit(1);   /* 修改：exit(-1) → exit(1)，-1 作为退出码会被截断成 255 */
    }
    LOG_INFO("检测到 %d 个网卡", g_iface_count);
    for (int i = 0; i < g_iface_count; i++) {
        LOG_INFO("  %s: %d.%d.%d.%d", 
                 g_ifaces[i].name,
                 g_ifaces[i].ip[0], g_ifaces[i].ip[1],
                 g_ifaces[i].ip[2], g_ifaces[i].ip[3]);
    }

    //键盘线程读取命令
    /*&KEY_T —— 线程标识符
    * NULL —— 线程属性,这里的 NULL：表示使用默认属性。也就是说，系统会按默认方式创建线程（通常是可合并的、默认栈大小）
    * key_pthread —— 线程执行函数
    * NULL —— 传递给线程函数的参数
    */
    pthread_t KEY_T;
    pthread_create(&KEY_T, NULL, key_pthread, NULL);
    pthread_detach(KEY_T);

    //创建原始套接字准备收包,创建数据接收缓冲区，数据长度接收
    /*PF_PACKET —— 协议族，作用：指定使用 Packet 套接字，
    * SOCK_RAW —— 套接字类型，作用：指定为原始套接字
    * htons(ETH_P_ALL) —— 协议类型，ETH_P_ALL：表示接收所有类型的以太网帧
    */
    /* ==================== 修改点（新增） ====================
     * 讲解: 初始化转发线程限流信号量（进程内使用，pshared=0）。
     * ========================================================= */
    sem_init(&g_thread_sem, 0, MAX_IP_THREADS);

    raw_sock_fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (raw_sock_fd < 0) {
        LOG_ERROR("socket创建失败");
        perror("socket");
        exit(1);   /* 修改：exit(-1) → exit(1)，-1 作为退出码会被截断成 255 */
    }

    /* ==================== 修改点（新增）：自环修复 ====================
     * 原: 未设置 PACKET_IGNORE_OUTGOING，原始套接字会把自己刚发出去的
     *     帧又收回来（ARP 请求、转发包都自环），触发无限转发 → 线程池
     *     被打满 → “转发线程数已达上限，丢包”疯狂刷屏。
     * 现: 让内核忽略本机发出的帧，从源头切断自环。
     * ========================================================= */
    {
        int ignore_out = 1;
        if (setsockopt(raw_sock_fd, SOL_PACKET, PACKET_IGNORE_OUTGOING,
                       &ignore_out, sizeof(ignore_out)) != 0) {
            /* 修改：ERROR → WARN——旧内核可能不支持该选项（ENOPROTOOPT），
             * 属可降级情况；源 MAC 过滤兜底仍能杜绝自环 */
            LOG_WARN("PACKET_IGNORE_OUTGOING 设置失败（老内核可能不支持）: %s", strerror(errno));
        }
    }
    char recv_buf[RECV_SIZE] = "";
    ssize_t recv_len = 0; //ssize_t —— 有符号整型
    //打印内存池状态
    int used, total, free_blocks;
    pool_stats(&g_packet_pool, &used, &total, &free_blocks);
    LOG_INFO("内存池初始化完成: 总块数=%d, 块大小=%d, 空闲=%d", 
             total, PACKET_POOL_SIZE, free_blocks);

    LOG_INFO("进入主循环，开始收包...");
    unsigned long packet_counter = 0;   /* 修改点（新增）：用于周期触发 ARP 清理 */
    while(1){
        memset(recv_buf, 0, RECV_SIZE);
        /*raw_sock_fd,套接字文件描述符
        * recv_buf,接收缓冲区指针
        * 接收缓冲区指针,缓冲区大小
        * 0,flags 标志 0 表示阻塞接收，也可以是 MSG_DONTWAIT、MSG_PEEK 等
        * NULL,发送方地址指针 NULL 表示不获取对端地址信息
        * NULL,地址长度指针 NULL 表示不返回地址长度
        */
        recv_len = recvfrom(raw_sock_fd, recv_buf, sizeof(recv_buf), 0, NULL, NULL);//这一步就开始接收数据了
        if(recv_len <= 0 || recv_len > RECV_SIZE){
            LOG_ERROR("recvfrom 接收数据异常");
            perror("recvfrom");
            continue;
        }
        //正式判断数据包类型
        /* ==================== 修改点（新增）：自环兜底 ====================
         * 讲解: 即使 PACKET_IGNORE_OUTGOING 失效（老内核/个别环境），
         *       也丢弃“源 MAC 是本机网卡”的帧——本机发出的帧不会
         *       再进入转发流程，双保险杜绝自环。
         * ========================================================= */
        {
            int i, self_frame = 0;
            for (i = 0; i < g_iface_count; i++) {
                if (memcmp(recv_buf + 6, g_ifaces[i].mac, 6) == 0) {
                    self_frame = 1;
                    break;
                }
            }
            if (self_frame) {
                continue;
            }
        }
        uint16_t eth_type = (recv_buf[12] << 8) | (recv_buf[13]);
        if(eth_type == 0x0806){
            /* 修改点（新增）：ARP 包最小长度校验（14 字节帧头 + 28 字节 ARP 头） */
            if (recv_len < 42) {
                LOG_DEBUG("ARP 包长度异常: %zd", recv_len);   /* 修改：WARN → DEBUG，畸形包是常态 */
                continue;
            }
            unsigned char ip[4];
            unsigned char mac[6];
            memcpy(mac, recv_buf + 22, 6);
            memcpy(ip, recv_buf + 28, 4);
            arp_update(ip, mac);
        }
        else if(eth_type == 0x0800){
            /* ==================== 修改点（原 → 现） ====================
             * 原: unsigned char *dst_ip = (unsigned char *)recv_buf + 30;
             *     // 30 = 14(以太网头) + 16(IP头内目的IP偏移)
             *     无长度校验、不读取 IHL，IP 头带选项时会取错位置
             * 现: 先校验长度与 IP 版本，再按 IHL 计算目的 IP 偏移
             * 讲解: 目的 IP 在 IP 头内偏移 16 字节；无选项时 IHL=20，
             *       帧内偏移 = 14 + 16 = 30。IP 头带选项时 IHL 更大，
             *       偏移 = 14 + 16 + (IHL*4 - 20)。
             * ========================================================= */
            if (recv_len < 34) {
                LOG_DEBUG("IP 包长度异常: %zd", recv_len);   /* 修改：WARN → DEBUG，畸形包是常态 */
                continue;
            }
            unsigned char ip_ver_ihl = recv_buf[14];
            if ((ip_ver_ihl >> 4) != 4) {
                LOG_DEBUG("非 IPv4 包，忽略");
                continue;
            }
            int ip_hdr_len = (ip_ver_ihl & 0x0F) * 4;
            unsigned char *dst_ip = (unsigned char *)recv_buf + 14 + 16 + (ip_hdr_len - 20);
            if(ip_filter_check(dst_ip)){
                LOG_DEBUG("IP %d.%d.%d.%d 在黑名单中，丢弃",
                        dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
                continue;
            }
            RECV_DATA *packet = (RECV_DATA *)pool_alloc(&g_packet_pool);
            if(packet== NULL){
                LOG_WARN("内存池满，丢弃包");   /* 修改：ERROR → WARN，资源紧张值得关注但非致命 */
                continue;
            }
            /* ==================== 修改点（新增） ====================
             * 讲解: 转发线程限流——sem_trywait 成功才占用一个槽位，
             *       并发线程达到 MAX_IP_THREADS 时直接丢包（不阻塞收包循环）；
             *       线程结束后在 ip_pthread 里 sem_post 归还槽位。
             * ========================================================= */
            if (sem_trywait(&g_thread_sem) != 0) {
                LOG_DEBUG("转发线程数已达上限，丢包");   /* 修改：WARN → DEBUG，高流量下是常态 */
                pool_free(&g_packet_pool, packet);
                continue;
            }
            LOG_DEBUG("pool_alloc 成功: packet=%p", packet);   /* 修改：INFO → DEBUG，避免每个包刷屏 */
            packet->data_len = recv_len;
            memcpy(packet->data, recv_buf, recv_len);
            pthread_t IP_T;
            /* 修改点（新增）：检查 pthread_create 返回值，失败时归还内存与信号量槽位 */
            if (pthread_create(&IP_T, NULL, ip_pthread, packet) != 0) {
                LOG_ERROR("pthread_create 失败，丢包");
                pool_free(&g_packet_pool, packet);
                sem_post(&g_thread_sem);
                continue;
            }
            pthread_detach(IP_T);
        }
        else{
            LOG_DEBUG("非 IP/ARP 包，已忽略");
        }
        /* 修改点（新增）：周期性清理超时 ARP 表项（原代码从未调用 arp_cleanup） */
        if (++packet_counter % ARP_CLEANUP_INTERVAL == 0) {
            arp_cleanup();
        }
    }
    LOG_INFO("主循环退出，开始清理...");
    close(raw_sock_fd);
    ip_filter_save();
    ip_filter_destroy();
    arp_destroy();
    /* ==================== 修改点（原 → 现） ====================
     * 原: pool_stats(&g_iface_count, &used, &total, &free_blocks);  // 参数类型错误
     *     pool_reset(&g_iface_count);
     * 现: pool_stats(&g_packet_pool, ...);
     *     pool_reset(&g_packet_pool);
     * 讲解: 原代码把 int 变量的地址当 STATIC_POOL* 传入，类型完全不匹配；
     *       修正为真正的内存池对象。注意本段位于 while(1) 之后，
     *       正常运行时不可达——正常退出应走 key_pthread 的 exit 命令
     *       （exit_handler 中已保存黑名单并清理）。
     * ========================================================= */
    pool_stats(&g_packet_pool, &used, &total, &free_blocks);
    pool_reset(&g_packet_pool);
    LOG_INFO("内存池已重置");
    LOG_INFO("路由器已安全退出");

    return 0;
}
