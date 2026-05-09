//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_PROTOCOL_H
#define DNSRELAY_PROTOCOL_H
#include "infra/stl.h"
#include <stdint.h>
#define MAX_PACKET_SIZE 512
#define QR(flags) ((flags >> 15)&1)

#define OPCODE(flags) ((flags >> 12)&0xf)

#define AA(flags) ((flags >> 10)&1)

#define TC(flags) ((flags >> 9)&1)

#define RD(flags) ((flags >> 8)&1)

#define RA(flags) ((flags >> 7)&1)

#define Z(flags) ((flags >> 4)&0x7)

#define RCODE(flags) ((flags)&0xf)
// typedef enum { V4,V6 } IpVersion;
// typedef struct {
//     IpVersion version;
//     union {
//         uint32_t ipv4;
//         __uint128_t ipv6;
//     } data;
// } IpAddr;

/**
 * DNS协议的header节
 *
 */
typedef struct  {
    uint16_t id;
    uint16_t flags;
    uint16_t questions;
    uint16_t answer_RRs;
    uint16_t authority_RRs;
    uint16_t additional_RRs;
} SectionHeader;

typedef struct {
    char* qname; //不定长字节自解码字段
    uint16_t qtype;
    uint16_t qclass;
}SectionQuestion; //要查询的域名信息

typedef struct {
    uint32_t ttl;
    uint16_t type;
    uint16_t class;
    uint16_t rdata_length;
    char* name;//域名
    char* rdata;
} ResourceRecord;

typedef struct {
    SectionHeader header;
    //这些可能有多条，数量由header给出,列表元素类型T = ResourceRecord*
    LinkedList*  question;
    LinkedList*  answer_RRs;
    LinkedList* authority_RRs;
    LinkedList* additional_RRs;
} DnsPacket;

/**
 *@brief 反序列化dns包，申请内存空间存储
 * @param raw_packet 原始dns包
 * @param size 包大小
 * @param dns_pack 解析后的dns包
 * @return 正常返回0，异常返回-1
 */
int deserialize_alloc(char* raw_packet,int size, DnsPacket** dns_pack) ;

/**
 * @brief 反序列化dns包，复用原有内存空间，这是为了避免频繁malloc和free.如果原有空间不够大，会自动扩容
 * @param raw_packet 原始dns包
 * @param size 原始dns包大小
 * @param dns_pack 解析后的dns包，传入指针的引用，无需预创建缓冲区
 * @return 正常返回0，异常返回-1
 */
int deserialize_reuse(char* raw_packet,int size, DnsPacket* dns_pack) ;

/**
 * @brief 将dns包结构序列化为网络字节序的字节流
 * @param dns_pack 要序列化的dns包
 * @param packet_buf 序列化缓冲区，函数会自动申请内存
 * @return 序列化后的dns包大小，异常返回-1
 */
int serialize_alloc(const DnsPacket* dns_pack,char** packet_buf) ;
/**
 * @brief 将dns包结构序列化为网络字节序的字节流
 * @param dns_pack 要序列化的dns包
 * @param packet_buf 序列化缓冲区，必须预先分配足够的内存！
 * @return 序列化后的dns包大小，异常返回-1
 */
int serialize_reuse(const DnsPacket* dns_pack,char* packet_buf) ;

/**
 *
 * @param dns_pack 释放该dns包占用的内存
 */
void free_pack(DnsPacket* dns_pack);

/**
 * 查询dns缓存
 * @param domain 要查询的域名
 * @param cache_answer 查询结果
 * @return 命中缓存返回0,未命中返回1,异常返回-1
 */
int lookup_cache(const char* domain,LinkedList* cache_answer) ;

/**
 * 将数据包转为字符串方便日志格式化输出，结尾带\0
 * @param dns_pack dns包
 * @return 字符串地址
 */
char* to_log_string_packet(const DnsPacket* dns_pack);

/**
 *根据下游请求构造上游请求并发送
 * @param client_pack 下游请求
 * @param socket_holder 套接字
 * @return
 */
int request_upstream(const DnsPacket* client_pack,SocketHolder socket_holder);
#endif //DNSRELAY_PROTOCOL_H