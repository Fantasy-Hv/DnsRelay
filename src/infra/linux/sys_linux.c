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
#endif
