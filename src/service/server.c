//
// Created by yian on 2026/5/8.
//
#include "dns/server.h"
#include "dns/cache.h"
#include "dns/config.h"
#include <sys/select.h>  // select函数和相关宏
#include <sys/time.h>    // struct timeval
#include <sys/types.h>   // fd_set类型
#include <unistd.h>      // close
int server_loop() {
    return 0;
}
int server_setup() {

    int cache_size ;
    if (getProperty(KEY_CACHE_SIZE, &cache_size))
        cache_size = VALUE_DEFAULT_CACHE_SIZE;
    cache_init(cache_size);
    //主循环
    server_loop();

    return 0;
}
int server_shutdown() {

    return 0;
}

