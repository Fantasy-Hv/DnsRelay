//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_PLATFORM_H
#define DNSRELAY_PLATFORM_H

#include <stdint.h>

typedef int64_t ms;


// 返回单调毫秒时间戳
ms sys_time_ms(void);


// 字节序转换函数：主机序 <-> 网络序（大端序）
// 2字节（16位）
void h2n_2(uint16_t* host);
void n2h_2(uint16_t* net);

// 4字节（32位）
void h2n_4(uint32_t* host);
void n2h_4(uint32_t* net);

// 8字节（64位）
void h2n_8(uint64_t* host);
void n2h_8(uint64_t* net);

// 16字节（128位，用于IPv6地址）
void h2n_16(const uint8_t host[16], uint8_t net[16]);
void n2h_16(const uint8_t net[16], uint8_t host[16]);
#endif //DNSRELAY_PLATFORM_H