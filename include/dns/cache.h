//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_CACHE_H
#define DNSRELAY_CACHE_H
#include "infra/stl.h"
// dns缓存全局单例，上层可直接从本接口读写缓存数据，需要线程安全的实现。
/**todo
 * 缓存结构体
 */
typedef struct {

}Cache;

/**
 * 缓存初始化,保证可重入
 * @return
 */
int dns_cache_init();
/**
 * @brief 添加缓存,需要实现缓存村淘汰算法
 * @return 添加成功返回0，异常返回-1
 */
int dns_cache_put(const char* domain_name,const LinkedList* ips);

int dns_cache_get(const char* domain_name, LinkedList* ips);


/**
 *清理过期缓存
 * @return
 */
int dns_cache_prune();

/**
 * 释放缓存
 * @return
 */
int dns_cache_free();
#endif //DNSRELAY_CACHE_H
