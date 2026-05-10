//
// Created by yian on 2026/5/10.
//
#include <stddef.h>
#include "infra/thread.h"
#include "server/daemon.h"
#include "dns/cache.h"
//缓存检查间隔，ms为单位
#define CACHE_HEARTBEAT_INTERVAL 4000
//定时检查
void* daemon_dnscache_ttl(void*) {
    while (1) {
        thread_sleep_ms(CACHE_HEARTBEAT_INTERVAL);
        dns_cache_prune();
    }
    return NULL;
}