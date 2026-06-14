#include <string.h>
#include <time.h>
#include <netinet/in.h>
#include "infra/utils.h"

// 返回单调毫秒时间戳
int64_t sys_time_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}


static int is_little_endian(void) {
    static int endian_checked = 0;
    static int is_little = 0;

    if (!endian_checked) {
        uint16_t test = 0x0001;
        is_little = (((uint8_t *) &test)[0] == 0x01) ? 1 : 0;
        endian_checked = 1;
    }
    return is_little;
}

// 内部辅助函数：8字节反转
static uint64_t swap_8(uint64_t value) {
    return ((value >> 0 & 0xFF) << 56) |
           ((value >> 8 & 0xFF) << 48) |
           ((value >> 16 & 0xFF) << 40) |
           ((value >> 24 & 0xFF) << 32) |
           ((value >> 32 & 0xFF) << 24) |
           ((value >> 40 & 0xFF) << 16) |
           ((value >> 48 & 0xFF) << 8) |
           ((value >> 56 & 0xFF) << 0);
}

// 内部辅助函数：16字节反转
static void swap_16(const uint8_t src[16], uint8_t dst[16]) {
    for (int i = 0; i < 16; i++)
        dst[i] = src[15 - i];
}

// ==================== 公开API：主机序 -> 网络序 ====================

void h2n_2(uint16_t* host) {
    *host = htons(*host);
}

void h2n_4(uint32_t* host) {
    *host = htonl(*host);
}

void h2n_8(uint64_t* host) {
    if (is_little_endian())
        *host =  swap_8(*host);

}

void h2n_16(const uint8_t host[16], uint8_t net[16]) {
    if (!host || !net) return;

    if (is_little_endian())
        swap_16(host, net);
     else
        memcpy(net,host,16); // 大端序机器直接拷贝

}

// ==================== 公开API：网络序 -> 主机序 ====================

void n2h_2(uint16_t *net) {
         *net =  ntohs(*net);
}

void n2h_4(uint32_t* net) {
        *net = ntohl(*net);
}

void n2h_8(uint64_t* net) {

    if (is_little_endian())
        *net = swap_8(*net);

}

void n2h_16(const uint8_t net[16], uint8_t host[16]) {
    if (!net || !host) return;

    if (is_little_endian())
        swap_16(net, host);
     else
        memcpy(host,net,16); // 大端序机器直接拷贝

}

