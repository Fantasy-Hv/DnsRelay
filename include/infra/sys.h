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
    unsigned int code;
    char* msg;
}Exception;
typedef int64_t ms;

void sys_init();
// 返回单调毫秒时间戳
ms sys_time_ms(void);

// 用于在错误传播链上添加错误信息,传入errno
void sys_hook_stacktrace(int err_no,const char* at);

// 获取上一次错误的调用栈信息,保证是有效字符串，不可重入，返回的字符串不要free,
char* sys_get_stacktrace(void);

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