//
// Created by yian on 2026/5/9.
//
//需要线程安全的实现，使用<threads.h>
#include "dns/cache.h"
/**
 * 缓存初始化,保证可重入
 * @return
 */
int dns_cache_init() {
    // 1.读取主机hosts文件

    return 0;
}

/**
 * @brief 添加缓存,需要实现缓存淘汰算法
 * @param domain_name 纯字符串。推荐使用树形结构实现域名缓存解析，例如特殊设计的trie树。
 * @param ips T = NetEnd*
 * @return 添加成功返回0，异常返回-1
 */
int dns_cache_put(const char* domain_name,const LinkedList* ips) {
    return 0;
}

/**
 * @brief 查询缓存
 * @param domain_name 纯字符串。
 * @param ips T = NetEnd*
 * @return 命中缓存返回0,未命中返回1
 */
int dns_cache_get(const char* domain_name, LinkedList* ips) {
    return 0;
}

/**
 *清理过期缓存
 * @return
 */
int dns_cache_prune() {
    return 0;
}
