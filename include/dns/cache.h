
// dns缓存全局单例，上层可直接从本接口读写缓存数据，需要线程安全的实现。(可以用<threads.h>提供的锁)
#ifndef DNSRELAY_CACHE_H
#define DNSRELAY_CACHE_H
#include "infra/stl.h"
#include "dns/protocol.h"
#define SECTION_DNS "dns"
#define KEY_IP_TABLE_PATH "iptable"
#define VALUE_DEFAULT_IP_TABLE_PATH "./dnsrelay.txt"
// 协议解析层与缓存层数据交互的领域模型
typedef struct {
    uint16_t answer_RRs; // 回答段的条目数量
    uint16_t authority_RRs; //权威段的条目数量
    uint16_t additional_RRs; // 附加段的条目数量
    /*
     *三个段的rr,必须按照answer、auth、additional的顺序
     * 添加缓存时，这个列表由上层创建,上层释放
     * 缓存需要做深拷贝
    */
    Vector* rrs ; //T = ResourceRecord*
} CacheValue;

/**
 * 缓存初始化，在此读取iptable
 * @return
 */
int dns_cache_init();

/**
 * 缓存RR记录，
 * @param cache_value 缓存记录，rr列表只读，内部做深拷贝
 * @return 0-缓存成功 1-缓存失败
 */
int dns_cache_put(const char* qname,Qtype type,Class class,CacheValue cache_value);
/**
 * 根据问题name和type class查询对应的RR
 * @param result 结果容器，rrs域会回填，所以rrs=NULL是允许的。如果缓存miss,rrs为NULL
 * @return 0-命中 ，1-miss，
 */
int dns_cache_get(const char* qname,Qtype type,Class qclass,CacheValue* result);


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
