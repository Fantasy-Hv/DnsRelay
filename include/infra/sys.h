//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_PLATFORM_H
#define DNSRELAY_PLATFORM_H
#define WINDOWS 0
#define LINUX 1
#include <stdint.h>

int64_t sys_time_ms(void);   // 跨平台返回单调毫秒时间戳
#endif //DNSRELAY_PLATFORM_H