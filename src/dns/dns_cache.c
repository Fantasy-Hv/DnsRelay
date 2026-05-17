//
// Created by yian on 2026/5/9.
//
//需要线程安全的实现，使用<threads.h>
#include "dns/cache.h"
#include <threads.h>
#include "dns/protocol.h"
/**
 * 缓存初始化,main会调用
 * @return
 */
int dns_cache_init() {
    // 1.读取主机hosts文件

    return 0;
}





/**
 *清理过期缓存
 * @return
 */
int dns_cache_prune() {
    return 0;
}
