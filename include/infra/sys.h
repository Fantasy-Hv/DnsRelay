//
// Created by yian on 2026/5/8.
//

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
ms sys_time_ms(void);   // 跨平台返回单调毫秒时间戳
Exception get_syscall_error(void); // 获取最后一次系统调用错误信息

#endif //DNSRELAY_PLATFORM_H