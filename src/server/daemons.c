//
// Created by yian on 2026/5/10.
//
#include <stddef.h>
#include <threads.h>
#include "server/daemon.h"
#include "dns/cache.h"
//缓存检查间隔，秒为单位
#define CACHE_HEARTBEAT_INTERVAL 4
//定时检查
int daemon_dnscache_ttl(void*) {
    struct timespec time_to_sleep = (struct timespec){CACHE_HEARTBEAT_INTERVAL,0};
    while (1) {
        thrd_sleep(&time_to_sleep,NULL);
        dns_cache_prune();
    };
}