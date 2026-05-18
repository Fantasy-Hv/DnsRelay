//
// Created by yian on 2026/5/8.
//
//一些时间相关的函数
#ifndef DNSRELAY_PLATFORM_H
#define DNSRELAY_PLATFORM_H
#define WINDOWS 0
#define LINUX 1
#include <stdint.h>
typedef struct {
    char* msg;
}Exception;
typedef int64_t ms;


// 返回单调毫秒时间戳
ms sys_time_ms(void);


// 获取系统hosts文件路径
char* sys_hosts_path();


// 字节序转换函数：主机序 <-> 网络序（大端序）
// 2字节（16位）
void host_to_net_2(uint16_t* host);
void net_to_host_2(uint16_t* net);

// 4字节（32位）
void host_to_net_4(uint32_t* host);
void net_to_host_4(uint32_t* net);

// 8字节（64位）
void host_to_net_8(uint64_t* host);
void net_to_host_8(uint64_t* net);

// 16字节（128位，用于IPv6地址）
void host_to_net_16(const uint8_t host[16], uint8_t net[16]);
void net_to_host_16(const uint8_t net[16], uint8_t host[16]);
#endif //DNSRELAY_PLATFORM_H