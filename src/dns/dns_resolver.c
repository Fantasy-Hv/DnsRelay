//
// Created by yian on 2026/5/9.
//
// 核心模块！！！其他模块可以没有，但是这个必须很完善。
#include "dns/protocol.h"
#include "dns/cache.h"
#include <stdlib.h>
#define DNS_REV_BUF_SIZE 1024
/**
 *目前需要满足的两类需求：
 一、dns包的序列化和反序列化
  1.序列化dns包
 * header可以直接强转然后转换字节序
 * question节需要根据header的qdcont循环解析
    每个question解析行为是固定的，name字段自解码，qtype和qclass定长需要转换字节序
  RR的解析：分别按照header中指定的数量来循环解析每条RR
  rr的解析，前面ttl、type、class、datalength都是定长，需要转换字节序，
  name需要解码，然后rdata直接copy就行了。
  总的来说没有复杂的逻辑
 *2.反序列化dns包
    header需要转换字节序，然后copy
    question需要循环列表，编码name,qtype、qclass转换字节序
    rr一样，定长字段转换字节序，name编码，rdata直接copy
 二、dns包内容解析，本质上是要回答：如何从一个包构造另一个包，需要仔细研究协议规定的行为

    1.header中flags的解析，需要根据值采取相应的行为
    2.question中内容的解析，
 */
/**
 * 将人类可读的域名字符串编码为协议可用的字符串
 * @return
 */
char* encode_name(const char*) {
    return NULL;
}

/**
 * 将dns包的域名字段解析为人类可读的字符串
 * @return
 */
char* decode_name(const char*) {
    return NULL;
}


/**
 * @brief 将dns包结构序列化为网络字节序的字节流
 * @param dns_pack 要序列化的dns包
 * @param packet_buf 序列化缓冲区，必须预先分配足够的内存！
 * @return 序列化后的dns包大小，异常返回-1
 */
int pack_serialize(const DnsPacket* dns_pack,char* packet_buf) {
    return 0;
}
/**
 * @brief 将网络字节流反序列化为dns包
 * @param raw_pack 网络字节流
 * @param packet
 * @return
 */
int pack_deserialize(const char* raw_pack,int len,DnsPacket** packet) {
    return 0;
}

/**
 * @param dns_pack 释放该dns包占用的内存
 */
void pack_free(DnsPacket* dns_pack) {
}


/**
 * 创建一个空dns包，主要是为了初始化RR列表
 * @return
 */
DnsPacket* pack_create() {
    return NULL;
}

/**
 * 包的比较函数，主要用于判等,仅比较header id.
 * @param packet1
 * @param packet2
 * @return
 */
int packet_equals(T packet1, T packet2) {
    DnsPacket* pack1 = packet1;
    DnsPacket* pack2 =packet2;
    return pack1->header.id - pack2->header.id;
}
/**
 * 将数据包转为字符串方便日志格式化输出，结尾带\0
 * @param dns_pack dns包
 * @return 字符串地址
 * 低优先级
 */
char* to_log_string_packet(const DnsPacket* dns_pack) {
    return NULL;
}
/**
 * 包是否为请求包
 * @param packet
 * @return 是-1,否-0
 */
int packet_is_query(const DnsPacket* packet) {
    return 0;
}
/**
 * 将下游的查询包转为发给上游的查询包
 * @param query_pack 客户端请求
 * @param relay_id 转发包使用的id
 * @param relay_pack 生成的转发包
 * @return
 */
int pack_make_relay(const DnsPacket * query_pack,uint16_t relay_id,DnsPacket** relay_pack) {
    return 0;
}


/**
 * 根据的上游响应构造下游响应。
 * @param recv 上游应答
 * @param send 返回给客户端的响应
 * @param client_id 客户端查询请求的id
 */
void pack_make_response_relay(const DnsPacket* recv,DnsPacket** send,uint16_t client_id) {

}

/**
 * 生成服务器内部失败响应包
 */
void pack_make_inner_error(const DnsPacket* query, DnsPacket** answer) {

}

PacketDirection pack_make_local_ans(const DnsPacket* query,DnsPacket** response) {
    return CLIENT;
}

