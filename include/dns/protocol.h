//
// Created by yian on 2026/5/8.
//
//无状态的dns协议解析层
#ifndef DNSRELAY_PROTOCOL_H
#define DNSRELAY_PROTOCOL_H
#include "infra/stl.h"
#include <stdint.h>
#include "infra/socket.h"
#include "dns/cache.h"
#define MAX_PACKET_SIZE 512
#define IS_QUERY(flags) ((flags >> 15)&1)

#define OPCODE(flags) ((flags >> 12)&0xf)

#define AA(flags) ((flags >> 10)&1)

#define TC(flags) ((flags >> 9)&1)

#define RD(flags) ((flags >> 8)&1)

#define RA(flags) ((flags >> 7)&1)

#define Z(flags) ((flags >> 4)&0x7)

#define RCODE(flags) ((flags)&0xf)
typedef enum {
    UPSTREAM,CLIENT
}PacketDirection;


/**
 * DNS协议的header节
 *
 */
typedef struct  {
    uint16_t id;
    uint16_t flags;
    uint16_t qcount;
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
    LinkedList*  questions;
    LinkedList*  answer_RRs;
    LinkedList* authority_RRs;
    LinkedList* additional_RRs;
} DnsPacket;


/**
 *
 * @param dns_pack 释放该dns包占用的内存
 */
void pack_free(DnsPacket* dns_pack);

/**
 * 创建一个空dns包，主要是为了初始化RR列表
 * @return
 */
DnsPacket* pack_create();

/**
 * 包的比较函数，主要用于判等,仅比较header id.
 * @param packet1
 * @param packet2
 * @return
 */
int packet_equals(T packet1, T packet2) ;
/**
 * 将数据包转为字符串方便日志格式化输出，结尾带\0
 * @param dns_pack dns包
 * @return 字符串地址
 */
char* to_log_string_packet(const DnsPacket* dns_pack);

/**
 * 包是否为请求包
 * @param packet
 * @return 是-1,否-0
 */
int packet_is_query(const DnsPacket* packet);

/**
 *对到来的请求包尝试构建本地应答
 *@param query 客户端请求包
*@param response 下一步要发送的dns包,如果本机可回答，返回响应包;
 *如果需要查询上游，返回null
 *@return CLIENT-构造成功，可以返回响应，UPSTREAM-构造失败，需要请求上游，response 指向NULL
 */
PacketDirection pack_answer_locally(const DnsPacket* query,DnsPacket** response);

/**
 * 将下游的查询包转为发给上游的查询包
 * @param query_pack 客户端请求
 * @param relay_id 转发包使用的id
 * @param relay_pack 生成的转发包
 * @return
 */
int packe_cook_relay(const DnsPacket * query_pack,uint16_t relay_id,DnsPacket** relay_pack) ;

/**
 *
 * @param recv 上游应答
 * @param send 返回给客户端的响应
 * @param client_id 客户端查询包的id
 */
void pack_cook_response(const DnsPacket* recv,DnsPacket** send,uint16_t client_id);

/**
 * @brief 将网络字节流反序列化为dns包
 * @param raw_pack 网络字节流
 * @param packet
 * @return
 */
int pack_deserialize(const char* raw_pack,int len,DnsPacket** packet) ;


/**
 * @brief 将dns包结构序列化为网络字节序的字节流
 * @param dns_pack 要序列化的dns包
 * @param packet_buf 序列化缓冲区，必须预先分配足够的内存！
 * @return 序列化后的dns包大小，异常返回-1
 */
int pack_serialize(const DnsPacket* dns_pack,char* packet_buf);
#endif //DNSRELAY_PROTOCOL_H