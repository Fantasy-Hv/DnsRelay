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

    return 0;
}


/**
 * 缓存RR记录
 * @param record
 * @return 0-缓存成功 1-缓存失败
 */
int dns_cache_put(const ResourceRecord * record) {
    return 0;
}

/**
 * 根据name和type查询对应的RR
 * @param name RR的name
 * @param type RR类型
 * @param result 结果列表，类型为T=ResourceRecord*,指向缓存中RR的拷贝，传入的列表必须有效,
 * @return 0-命中 ，1-miss
 */
int dns_cache_get(const char* qname,Qtype type,Class qclass,Vector* result) {
    return 0;
}


/**
 *清理过期缓存,
 * @return 0 正常，-1失败
 */
int dns_cache_prune() {
    return 0;
}
