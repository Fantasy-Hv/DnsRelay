//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_CACHE_H
#define DNSRELAY_CACHE_H
#include "infra/socket.h"
#include "infra/stl.h"
// dns缓存全局单例，上层可直接从本接口读写缓存数据，需要线程安全的实现。
//这里也是整个系统主要占用内存的部分，另一个是session
/**todo

   使用哈希表和链表实现缓存
   LRU可以在链表上比较容易地实现。
   一个缓存记录的数据类型
   char* 指向域名
   过期检查只需要遍历链表，然后通过哈希表精确定位检查删除
   而lru的运行：
    每次命中一个就记录，就将该记录移动到链表头，淘汰的时候将

 * NetEnd ~ char*
 */
typedef struct {

}Cache;

/**
 * 缓存初始化,保证可重入
 * @return
 */
int dns_cache_init();
/**
 * @brief 添加缓存,需要实现缓存淘汰算法
 * @param domain_name 域名的人类可读字符串表示。
 * @param ips T = NetEnd*
 * @return 添加成功返回0，异常返回-1
 */
int dns_cache_put(const char* domain_name,const LinkedList* ips);
/**
 * @brief 查询缓存
 * @param domain_name 纯字符串。
 * @param ips T = NetEnd*
 * @return 命中缓存返回0,未命中返回1
 */
int dns_cache_get(const char* domain_name, LinkedList* ips);

/**
 *
 * @param ip 要查询的ip
 * @param domain_names ip对应的域名，可能有多个，列表元素T=char*
 * @return
 */
int dns_cache_reverse_get( NetEnd ip,const LinkedList* domain_names);
/**
 *
 * @param ip
 * @param domain_names ip对应的域名，可能有多个，列表元素T=char*
 * @return
 */
int dns_cache_reverse_put( NetEnd ip,const LinkedList* domain_names);
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
