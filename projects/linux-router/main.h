#ifndef __MAIN_H__
#define __MAIN_H__

/* ==================== 修改点（原 → 现） ====================
 * 原: int raw_sock_fd;   // 头文件里直接“定义”全局变量
 * 现: extern int raw_sock_fd;  // 只声明，定义移到 main.c
 * 讲解: 头文件会被多个 .c 包含，若在其中定义变量，每个编译单元都会
 *       生成一个定义（GCC 默认 -fcommon 下不报错，但属于未定义行为，
 *       -fno-common 下会直接链接失败）。extern 只声明、在 main.c
 *       唯一处定义才是正确做法。
 * ========================================================= */
extern int raw_sock_fd;
/* ==================== 修改点（原 → 现） ====================
 * 原: #define RECV_SIZE 1500
 * 现: #define RECV_SIZE 1518
 * 讲解: 以太网最大帧为 1514 字节（14 字节帧头 + 1500 字节载荷）。
 *       原值 1500 会让 recvfrom() 把满 MTU 的包截断 14 字节，
 *       转发出去的包是残缺的；1518 可完整容纳标准最大帧。
 * ========================================================= */
#define RECV_SIZE 1518
#define PACKET_POOL_COUNT 64
#define PACKET_POOL_SIZE 2048

typedef struct recv_data //收到的数据
{
    ssize_t data_len; //数据长度
    unsigned char data[RECV_SIZE]; //接收缓冲区
}RECV_DATA;

#endif // !__MAIN_H__

