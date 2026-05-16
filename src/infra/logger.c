//
// Created by yian on 2026/5/8.
//日志打印工具类，
//
#include "infra/logger.h"

#include <stdarg.h>
#include <stdio.h>

#include "infra/config.h"
#include "infra/sys.h"
static char* LEVEL_STR[4]={"DEBUG","INFO","WARN","ERROR"};
static  LogLevel logging_level = INFO; //日志过滤级别
static FILE* output_channels[4] ; // 各级别的输出流
// todo
void* log_config_parser(const char* key,const char* value) {
    return NULL;
}

int logger_init() {
    config_register_parser(LOG_SECTION,&log_config_parser);
    config_get(LOG_SECTION,KEY_LOG_LEVEL, &logging_level);
    output_channels[DEBUG]=stdout;
    output_channels[INFO]=stdout;
    output_channels[WARN]=stdout;
    output_channels[ERROR]=stderr;
    return 0;
}

void do_log(LogLevel level, const char *format, ...) {
    if (level<logging_level)return;
    FILE* channel = output_channels[level];
    //添加日志时间
    fprintf( channel,"%s[%s] ==> ",sys_datetime_now(),LEVEL_STR[level]);
    //输出日志内容
    va_list args = {0} ; // 用这个变量指向参数列表
    va_start(args,format); //初始化
    vfprintf(channel,format,args); // 格式化输出
    va_end(args); // 释放参数列表
    fprintf(channel,"\n");
}
