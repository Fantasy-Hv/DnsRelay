//
// Created by yian on 2026/5/8.
//
#pragma once

#ifndef DNSRELAY_SOCKET_H
#include <stddef.h>
/**
 * socket抽象层,以简便易用为目标
 */
#define DNSRELAY_SOCKET_H
typedef enum TransportProtocol {
    TCP,UDP
}TransportProtocol;
typedef enum {
    IPV4,IPV6
}
IpVersion;
typedef union {
    uint32_t ipv4;
    uint8_t ipv6[16];
}in_addr;
typedef struct {
    IpVersion version;
    in_addr addr;
}IpAddr;
// socket句柄/描述符
typedef void* SocketHolder;

int socket_create(TransportProtocol protocol,SocketHolder socket);

int socket_bind(SocketHolder socket,int port);

int socket_send_async(SocketHolder socket,const void *buf, size_t buf_len,IpAddr dest);

/**
 * 阻塞接收数据
 * @param socket
 * @param buf
 * @param buf_len 缓冲区大小
 * @return 收到的数据长度，没有数据返回0，异常返回-1
 */
int socket_recv_wait(SocketHolder socket,void *buf, size_t buf_len,IpAddr* source);

/**
 * 非阻塞接收数据
 * @param socket
 * @param buf
 * @param buf_len 缓冲区大小
 * @return 收到的数据长度，没有数据返回0，异常返回-1
 */
int socket_recv_nowait(SocketHolder socket,void *buf, size_t buf_len,IpAddr *source);

int socket_release(SocketHolder socket);


#endif //DNSRELAY_SOCKET_H