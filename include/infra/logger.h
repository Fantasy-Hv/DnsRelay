//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_LOG_H
#define DNSRELAY_LOG_H
typedef enum {
    DEBUG, INFO, WARN, ERROR
}LogLevel;
int do_log(LogLevel level,const char* format, ...);
void set_level(LogLevel level);
#endif //DNSRELAY_LOG_H