//
// Created by yian on 2026/5/8.
//
#ifndef DNSRELAY_SOCKET_H

#define DNSRELAY_SOCKET_H
#include <stddef.h>
#include <stdint.h>

#include "sys.h"
/**
 * socket抽象层,以简便易用为目标
 */

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
    int port;
}NetEnd;
// socket句柄/描述符
#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET SocketHolder;
#define SYS_INVALID_SOCKET INVALID_SOCKET
#else
typedef int SocketHolder;;
#define SYS_INVALID_SOCKET (-1)
#endif

int socket_create(TransportProtocol protocol,SocketHolder socket);

int socket_bind(SocketHolder socket,int port);

/**
 * 发送数据
 * @param socket
 * @param buf 要发送的数据
 * @param buf_len 数据长度
 * @param dest 发送目标
 * @return
 */
int socket_send(SocketHolder socket,const void *buf, size_t buf_len,NetEnd dest);


/**
 * 非阻塞接收数据
 * @param socket
 * @param buf
 * @param buf_len 缓冲区大小
 * @return 收到的数据长度，没有数据返回0，异常返回-1
 */
int socket_recv_nowait(SocketHolder socket,void *buf, size_t buf_len,const NetEnd *source);

int socket_release(SocketHolder socket);

/**
 * 阻塞当前线程，如果socket有数据或者超时，唤醒线程
 * @param socket_holder
 * @param timeout 超时时间，如果为负数表示永不超时
 * @param socket_cnt 等待的socket数量
 * @return
 */
int select(const SocketHolder *socket_holder,int socket_cnt,ms timeout);
#endif //DNSRELAY_SOCKET_H