//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_LOG_H
#define DNSRELAY_LOG_H
//日志输出方式：0.stdout/err 1.file
#define KEY_LOG_LEVEL "log_level"

#define LOG_SECTION "log"
typedef enum {
     TRACE=0,DEBUG,INFO,WARN,ERROR
}LogLevel;

int logger_init();

/**
 * 格式化打印日志，自动换行
 * @param level
 * @param format
 * @param ...
 */
void do_log(LogLevel level,const char* format, ...);

#endif //DNSRELAY_LOG_H