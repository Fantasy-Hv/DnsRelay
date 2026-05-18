//
// Created by yian on 2026/5/18.
//
#include "infra/exception.h"

#include <string.h>
thread_local  char msg [2048] ;
thread_local char stk_open = 0;
thread_local char err_flag = 0;



void ex_try() {
    err_flag = 1;
    msg[0]='\0';
}
int ex_catch() {
    return msg[0]!='\0';
}
/*fixme 需要缓冲区溢出检查
 */
void ex_throw(const char* at) {
    int net_r = strlen(msg);
    memcpy(&msg[net_r],at,strlen(at)+1); // 带 \0
    net_r+= strlen(at);
    msg[net_r++] = '-';
    err_flag = 1;
}


// 获取错误调用栈信息，返回只读字符串
const char* ex_end(void) {
    if (!err_flag)return "";
    err_flag = 0;
    return msg;
}

