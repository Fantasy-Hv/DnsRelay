//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_CACHE_H
#define DNSRELAY_CACHE_H
#include "infra/stl.h"
#include "dns/protocol.h"
// dns缓存全局单例，上层可直接从本接口读写缓存数据，需要线程安全的实现。
//这里也是整个系统主要占用内存的部分，另一个是session
/**todo

   使用哈希表和链表实现缓存
   LRU可以在链表上比较容易地实现。
   一个缓存记录的数据类型
   过期检查只需要遍历链表，然后通过哈希表精确定位检查删除
   而lru的运行：
    每次命中一个就记录，就将该记录移动到链表头，淘汰的时候将链表末尾的淘汰

 * NetEnd ~ char*
 *
 * dns缓存的业务--缓存RR，
 * A 记录
 * AAAA 记录
 * CNAME 记录
 * NS 记录
 * MX 记录
 * PTR 记录 （反向查询记录）
 * 每类记录一个哈希表，key为name.
 */

/**
 * 缓存初始化,保证可重入
 * @return
 */
int dns_cache_init();

/**
 * 缓存RR记录
 * @param record
 * @return 0-缓存成功 1-缓存失败
 */
int dns_cache_put(const ResourceRecord * record);

/**
 * 根据name和type查询对应的RR
 * @param name RR的name
 * @param type RR类型
 * @param result 结果列表，类型为T=ResourceRecord*，传入的列表必须有效
 * @return 0-命中 ，1-miss
 */
int dns_cache_get(const char* qname,Qtype type,Class qclass,LinkedList* result);


/**
 *清理过期缓存
 * @return
 */
int dns_cache_prune();

/**
 * 清空所有缓存
 * @return
 */
int dns_cache_free();
#endif //DNSRELAY_CACHE_H
