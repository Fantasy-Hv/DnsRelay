//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_CACHE_H
#define DNSRELAY_CACHE_H
#include "infra/stl.h"
#include "dns/protocol.h"

// dns缓存全局单例，上层可直接从本接口读写缓存数据，需要线程安全的实现。(可以用<threads.h>提供的锁)



/**
 * 缓存初始化
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
 * 按问题三元组缓存一整组回答。
 * 这是真正的主缓存接口，用于缓存一次查询对应的完整 answer 集合。
 * @param question 查询问题
 * @param records 回答RR列表，元素类型 T = ResourceRecord*
 * @return 0-缓存成功 1-缓存失败
 */
int dns_cache_put_answer_set(const SectionQuestion *question, Vector *records);

/**
 * 根据name和type查询对应的RR
 * @param name RR的name
 * @param type RR类型
 * @param result 结果列表，类型为T=ResourceRecord*,指向缓存中RR的拷贝，传入的列表必须有效,
 * @return 0-命中 ，1-miss
 */
int dns_cache_get(const char* qname,Qtype type,Class qclass,Vector* result);


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
