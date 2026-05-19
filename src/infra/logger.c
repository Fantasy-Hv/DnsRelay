//
// Created by yian on 2026/5/8.
//日志打印工具类，
//
#include "infra/logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "infra/config.h"
#include "infra/utils.h"
#define  LEVEL_NUM 5
static char* LEVEL_STR[LEVEL_NUM]={"TRACE","DEBUG","INFO","WARN","ERROR"};
static  LogLevel logging_level = INFO; //日志过滤级别
static FILE* output_channels[LEVEL_NUM] ; // 各级别的输出流
//日志模块的配置值都是基本类型，因此不用注册配置清理函数
int log_config_parser(const char* key,const char* value,T* result) {
    if (strcmp(key,KEY_LOG_LEVEL)) {
        LogLevel level = TRACE;
        while (level<LEVEL_NUM&&!strcasecmp(LEVEL_STR[level],value))
            level++;
        if (level<LEVEL_NUM) {
            *result = (T)level;
            return 0;
        }
        *result = (T)INFO;
        return 0;
    }
    return 0;
}

int logger_init() {
    config_register_parser(LOG_SECTION,log_config_parser);
    config_get(LOG_SECTION,KEY_LOG_LEVEL, (T*)&logging_level);
    output_channels[TRACE]=stdout; // 跟踪程序运行
    output_channels[DEBUG]=stdout; // 调试信息
    output_channels[INFO]=stdout; // 程序运行的关键节点
    output_channels[WARN]=stdout; // 警告，可能会导致错误
    output_channels[ERROR]=stderr; // 错误
    return 0;
}

void do_log(LogLevel level, const char *format, ...) {
    if (level<logging_level)return;
    FILE* channel = output_channels[level];
    //添加日志时间
    fprintf( channel,"%ld[%s] ==> \n",sys_time_ms()/1000,LEVEL_STR[level]);
    //输出日志内容
    va_list args = {0} ; // 用这个变量指向参数列表
    va_start(args,format); //初始化
    vfprintf(channel,format,args); // 格式化输出
    va_end(args); // 释放参数列表
    fprintf(channel,"<==\n");
}
