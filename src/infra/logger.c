//
// Created by yian on 2026/5/8.
//日志打印单例类，
//
#include "infra/logger.h"
static  LogLevel logging_level;
static  int level_set = 0;
void set_level(LogLevel level) {
    if (!level_set) { // 只设置一次
        logging_level = level;
        level_set ^=1;
    }
}

int do_log(LogLevel level, const char *format, ...) {
    return 0;
}
