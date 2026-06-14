#include "infra/exception.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define  MSG_SIZE (1<<12)
thread_local  char msg [MSG_SIZE] ; // 调用栈
thread_local char err_flag = 0;
thread_local int stack_top = MSG_SIZE-1; // 使用递减满栈
/**
 * err_flag : 是否开启错误记录
 * stack_top < MSG_SIZE -1 : 是否有错误记录
 */
void ex_try() {
    err_flag = 1;
    stack_top = MSG_SIZE-1;
    msg[stack_top]='\0'; //最后一个字节是\0
}
int ex_catch() {
    return err_flag&&stack_top<MSG_SIZE-1;
}
/*
 */
void ex_throw(const char* format,...) {
    if (!err_flag) ex_try();
    char at[512] = {'\0'};

    // 格式化字符串（限制长度，防止栈溢出）
    va_list args = {0};
    va_start(args, format);
    vsnprintf(at, sizeof(at), format, args);
    va_end(args);

    // 写入错误信息栈
    int len = (int) strlen(at);
    if (len == 0) return;
    int net_r = stack_top - len;
    if (net_r < 0) return; // 栈空间不足，丢弃
    memcpy(&msg[net_r], at, len); // 不带 \0
    msg[--net_r] = '\n';
    stack_top = net_r;
}


// 获取错误调用栈信息，返回只读字符串
const char* ex_end(void) {
    if (!err_flag) return "";
    err_flag = 0;
    return &msg[stack_top];
}

