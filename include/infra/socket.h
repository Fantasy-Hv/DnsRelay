//
// Created by yian on 2026/5/8.
//
#ifndef DNSRELAY_SOCKET_H
#define DNSRELAY_SOCKET_H
#include <stddef.h>
#include <stdint.h>

#include "sys.h"
/**
 * UDP socket 抽象层，对系统调用的简单封装
 */

typedef enum {
    IPV4,IPV6
}
IpVersion;

// 网络序地址（大端）
typedef union {
    uint32_t ipv4;
    uint8_t ipv6[16];
}in_addr;

typedef struct {
    in_addr addr;
    IpVersion version;
    int port; // 网络序
}NetEnd;

#ifdef _WIN32
#include <winsock2.h>
// socket句柄/描述符
typedef SOCKET SocketHolder;
#define SYS_INVALID_SOCKET INVALID_SOCKET
#else
// socket句柄/描述符
typedef  int  SocketHolder;;
#define SYS_INVALID_SOCKET (-1)
#endif

int socket_create(SocketHolder *socket_holder);

/**
 *
 * @param socket 通过socket_create得到的holder
 * @param port 端口号，主机序
 * @return
 */
int socket_bind(SocketHolder socket,uint16_t port);

/**
 * 发送数据
 * @param socket
 * @param buf 要发送的数据
 * @param buf_len 数据长度
 * @param dest 发送目标
 * @return  >0-拷贝到内核缓冲区的字节数，0-发送了0字节，-1-错误，需检查errno
 */
int socket_send(SocketHolder socket,const void *buf, size_t buf_len,NetEnd dest);


/**
 * 非阻塞接收数据
 * @param socket
 * @param buf
 * @param buf_len 缓冲区大小
 * @return 收到的数据长度，没有数据返回0，异常返回-1
 */
int socket_recv_nowait(SocketHolder socket,void *buf, size_t buf_len, NetEnd *source);

int socket_release(SocketHolder socket);

/**
 * 系统调用select的封装
 * 阻塞当前线程，如果socket有数据或者超时，唤醒线程。
 * @param socket_holder 监听的socket列表，not null
 * @param timeout 超时时间，如果为负数表示永不超时
 * @param socket_cnt 等待的socket数量
 * @return >0,表示有可读的socket,=0表示超时，<0 错误
 */
int socket_sleep_on(SocketHolder socket_holder,int socket_cnt,ms timeout);
#endif //DNSRELAY_SOCKET_H