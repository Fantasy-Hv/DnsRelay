//
// Created by yian on 2026/5/8.
//

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
 * 缓存初始化
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

/**
 * 加载 IP 映射表（dnsrelay.txt），将静态映射条目写入缓存。
 * 文件中每行格式: IP = domain1,domain2,...,(c)alias_domain
 * - 普通域名: 创建 A/AAAA RR，ttl = UINT32_MAX（永不过期）
 * - (c) 前缀: 创建 CNAME RR，rdata 指向同行的第一个非 (c) 域名
 * - IP 为 0.0.0.0: 封禁该域名（rdata 全零，后续 validate 返回 NXDOMAIN）
 *
 * 映射文件路径通过 config_get("dns","iptable") 读取，
 * 若未配置则默认为 "./dnsrelay.txt"。
 *
 * @return 0-成功，-1 失败
 */
int dns_cache_load_ip_table();
#endif //DNSRELAY_CACHE_H
