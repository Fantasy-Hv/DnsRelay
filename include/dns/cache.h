//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_CACHE_H
#define DNSRELAY_CACHE_H
#include "infra/stl.h"

/**todo
 * 缓存结构体
 */
typedef struct {

}Cache;
/**
 *@brief
 *初始化缓存对象:
 * 从持久化存储中加载缓存,
 * 从hosts文件加载缓存.
 * @param capacity 缓存容量,每个域名占1个单元
 * @return 初始化成功返回0，异常返回-1
 */
Cache* cache_create(int capacity);


/**
 * @brief 添加缓存,需要实现缓存村淘汰算法
 * @return 添加成功返回0，异常返回-1
 */
int put(const char* domain_name,const LinkedList* ips);

int get(const char* domain_name, LinkedList* ips);


/**
 *清理过期缓存
 * @return
 */
int prune();

/**
 * 释放缓存
 * @return
 */
int cache_free(Cache * cache);
#endif //DNSRELAY_CACHE_H
