//
// Created by yian on 2026/5/8.
//日志打印单例类，
//
#include "infra/logger.h"

#include <stdio.h>

#include "../../include/infra/config.h"
static  LogLevel logging_level;
static LogOutput output_mode;
int (*log_output)( const char *, ...);

void log_init() {
    config_get(KEY_LOG_LEVEL, &logging_level);
    config_get(KEY_LOG_OUTPUT, &output_mode);
    if (output_mode==LOG_STDOUT)
        log_output = printf;
}

void do_log(LogLevel level, const char *format, ...) {
    if (level<logging_level)return;
    //添加日志时间

}
