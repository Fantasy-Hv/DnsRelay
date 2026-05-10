//
// Created by yian on 2026/5/9.
//
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
int packet_comparator(T packet1, T packet2) {
    DnsPacket* pack1 = packet1;
    DnsPacket* pack2 =packet2;
    return pack1->header.id - pack2->header.id;
}
/**
 * 将数据包转为字符串方便日志格式化输出，结尾带\0
 * @param dns_pack dns包
 * @return 字符串地址
 */
char* to_log_string_packet(const DnsPacket* dns_pack) {
    return NULL;
}

/**
 *解析dns查询包，并构建下一步要发送的包
 *@param recv 接收到的dns包
 *@param send 下一步要发送的dns包,如果是发给上游的请求，本方法会填写代理id.
 *@return 发送的方向：CLIENT为要发送给客户端，UPSTREAM为要发送给上游服务器
 */
PacketDirection cook_query(const DnsPacket* recv,DnsPacket** send) {
    return CLIENT;
}

/**
 * 根据的上游响应构造下游响应。
 * @param recv 上游应答
 * @param send 返回给客户端的响应
 * @param client_id 客户端查询请求的id
 */
void pack_cook_response(const DnsPacket* recv,DnsPacket** send,uint16_t client_id){}


int protocol_init() {
    dns_cache_init();
    return 0;
}
