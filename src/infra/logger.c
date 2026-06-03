#include "infra/logger.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "infra/config.h"
#include "infra/utils.h"
#define  LEVEL_NUM 5
static char* LEVEL_STR[LEVEL_NUM]={"TRACE","DEBUG","INFO","WARN","ERROR"};
static  LogLevel logging_level = INFO; //日志过滤级别
static FILE *output_channels[LEVEL_NUM]; // 各级别的输出流
//日志模块的配置值都是基本类型，因此不用注册配置清理函数
int log_config_parser(const char *key, const char *value, T *result) {
    if (!strcmp(key,KEY_LOG_LEVEL)) {
        LogLevel level = TRACE;

        while (level < LEVEL_NUM && strcasecmp(LEVEL_STR[level], value) != 0)
            level++;

        level = level < LEVEL_NUM ? level : INFO;
        *result = (T) level;
        return 0;
    }
    for (int i = TRACE; i < LEVEL_NUM; i++) {
        if (strcasecmp(LEVEL_STR[i], key) == 0) {
            if (strcasecmp(value, "stdout") == 0)
                *result = stdout;
            else if (strcasecmp(value, "stderr") == 0)
                *result = stderr;
            else {
                FILE *out = fopen(value, "a+");

                if (out == NULL) {
                    printf("ERROR: log file %s open failed %d,check config\n", value,errno);
                    *result = stdout;
                } else *result = out;
            }
        }
    }
    return 0;
}

int logger_init() {
    config_register_parser(LOG_SECTION, log_config_parser);
    // 日志级别
    config_get(LOG_SECTION,KEY_LOG_LEVEL, (T *) &logging_level);
    // 日志输出通道
    for (int i = logging_level; i < LEVEL_NUM; i++) {
        output_channels[i] = stdout;
        config_get(LOG_SECTION, LEVEL_STR[i], (T *) &output_channels[i]);
    }

    return 0;
}

/**

 * @param level
 * @param format
 * @param ...
 */
void do_log(LogLevel level, const char *format, ...) {
    if (level < logging_level)return;
    FILE *channel = output_channels[level];
    //添加日志时间
    fprintf(channel, "t:%ld [%s] ", sys_time_ms() / 1000, LEVEL_STR[level]);
    //输出日志内容,下面这个是处理变参输出的模板代码
    va_list args = {0}; // 用这个变量指向可变参数列表
    va_start(args, format); //初始化
    vfprintf(channel, format, args); // 格式化输出
    va_end(args); // 释放参数列表
    fprintf(channel, "\n");
    fflush(channel);
}
