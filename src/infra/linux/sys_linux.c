//
// Created by yian on 2026/5/8.
//
#ifdef __linux__
#include <errno.h>
#include <time.h>

#include "infra/sys.h"
//todo // 跨平台返回单调毫秒时间戳
int64_t sys_time_ms(void) {
    return 0;
}
//todo
Exception get_syscall_error(void) {
    return (Exception){errno,""};
} // 获取最后一次系统调用错误信息

static int is_little_endian(void) {
    static int endian_checked = 0;
    static int is_little = 0;

    if (!endian_checked) {
        uint16_t test = 0x0001;
        is_little = (((uint8_t*)&test)[0] == 0x01) ? 1 : 0;
        endian_checked = 1;
    }
    return is_little;
}
// 内部辅助函数：2字节反转
static uint16_t swap_2(uint16_t value) {
    return ((value & 0x00FF) << 8) |
           ((value & 0xFF00) >> 8);
}

// 内部辅助函数：4字节反转
static uint32_t swap_4(uint32_t value) {
    return ((value & 0x000000FF) << 24) |
           ((value & 0x0000FF00) << 8)  |
           ((value & 0x00FF0000) >> 8)  |
           ((value & 0xFF000000) >> 24);
}

// 内部辅助函数：8字节反转
static uint64_t swap_8(uint64_t value) {
    return ((value >> 0  & 0xFF) << 56) |
           ((value >> 8  & 0xFF) << 48) |
           ((value >> 16 & 0xFF) << 40) |
           ((value >> 24 & 0xFF) << 32) |
           ((value >> 32 & 0xFF) << 24) |
           ((value >> 40 & 0xFF) << 16) |
           ((value >> 48 & 0xFF) << 8)  |
           ((value >> 56 & 0xFF) << 0);
}

// 内部辅助函数：16字节反转
static void swap_16(const uint8_t src[16], uint8_t dst[16]) {
    if (!src || !dst) return;

    dst[0]  = src[15];
    dst[1]  = src[14];
    dst[2]  = src[13];
    dst[3]  = src[12];
    dst[4]  = src[11];
    dst[5]  = src[10];
    dst[6]  = src[9];
    dst[7]  = src[8];
    dst[8]  = src[7];
    dst[9]  = src[6];
    dst[10] = src[5];
    dst[11] = src[4];
    dst[12] = src[3];
    dst[13] = src[2];
    dst[14] = src[1];
    dst[15] = src[0];
}

// ==================== 公开API：主机序 -> 网络序 ====================

uint16_t host_to_net_2(uint16_t host) {
    // 只有小端序机器才需要转换
    if (is_little_endian()) {
        return swap_2(host);
    }
    return host; // 大端序机器不需要转换
}

uint32_t host_to_net_4(uint32_t host) {
    if (is_little_endian()) {
        return swap_4(host);
    }
    return host;
}

uint64_t host_to_net_8(uint64_t host) {
    if (is_little_endian()) {
        return swap_8(host);
    }
    return host;
}

void host_to_net_16(const uint8_t host[16], uint8_t net[16]) {
    if (!host || !net) return;

    if (is_little_endian()) {
        swap_16(host, net);
    } else {
        // 大端序机器直接拷贝
        for (int i = 0; i < 16; i++) {
            net[i] = host[i];
        }
    }
}

// ==================== 公开API：网络序 -> 主机序 ====================

void net_to_host_2(uint16_t *net) {
    // 网络序是大端序，如果机器是小端序则需要转换
    if (is_little_endian())
         *net =  swap_2(net);
}

uint32_t net_to_host_4(uint32_t net) {
    if (is_little_endian()) {
        return swap_4(net);
    }
    return net;
}

uint64_t net_to_host_8(uint64_t net) {
    if (is_little_endian()) {
        return swap_8(net);
    }
    return net;
}

void net_to_host_16(const uint8_t net[16], uint8_t host[16]) {
    if (!net || !host) return;

    if (is_little_endian()) {
        swap_16(net, host);
    } else {
        // 大端序机器直接拷贝
        for (int i = 0; i < 16; i++) {
            host[i] = net[i];
        }
    }
}
#endif
