//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_LOG_H
#define DNSRELAY_LOG_H
//日志输出方式：0.stdout 1.file 2.syslog
#define KEY_LOG_OUTPUT "log_output"
#define KEY_LOG_LEVEL "log_level"
typedef enum {
     LOG_STDOUT,LOG_FILE,LOG_SYSLOG
}LogOutput;

typedef enum {
     ERROR ,WARN,INFO,DEBUG
}LogLevel;
void log_init();
void do_log(LogLevel level,const char* format, ...);
#endif //DNSRELAY_LOG_H