#include <threads.h>
#include "server/daemon.h"
#include "dns/cache.h"
#include "infra/exception.h"
#include "infra/logger.h"
//缓存检查间隔，秒为单位
#define CACHE_HEARTBEAT_INTERVAL 4
//定时检查
int daemon_dnscache_ttl(void*) {
    struct timespec time_to_sleep = (struct timespec){CACHE_HEARTBEAT_INTERVAL, 0};
    while (1) {
        ex_try();
        thrd_sleep(&time_to_sleep,NULL);
        dns_cache_prune();
        do_log(DEBUG, "cache ttl checked");
        if (ex_catch())
            do_log(ERROR, "daemon_cache_ttl :%s", ex_end());
    }
}