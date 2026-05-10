//
// Created by yian on 2026/5/9.
//
// 核心模块！！！其他模块可以没有，但是这个必须很完善。
#include "dns/protocol.h"
#include "dns/cache.h"
#include <stdlib.h>
#define DNS_REV_BUF_SIZE 1024
/**
 *@brief 反序列化dns包，申请内存空间存储
 * @param raw_packet 原始dns包
 * @param size 包大小
 * @param dns_pack 解析后的dns包
 * @return 正常返回0，异常返回-1
 */
int deserialize(char* raw_packet,int size, DnsPacket** dns_pack) {

    return 0;
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
 *
 * @param dns_pack 释放该dns包占用的内存
 */
void pack_free(DnsPacket* dns_pack){}


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
int packe_cook_relay(const DnsPacket * query_pack,uint16_t relay_id,DnsPacket** relay_pack) {
    return 0;
}


/**
 * 根据的上游响应构造下游响应。
 * @param recv 上游应答
 * @param send 返回给客户端的响应
 * @param client_id 客户端查询请求的id
 */
void pack_cook_response(const DnsPacket* recv,DnsPacket** send,uint16_t client_id){}


PacketDirection pack_answer_locally(const DnsPacket* query,DnsPacket** response) {
    return CLIENT;
}

