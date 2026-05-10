//
// Created by yian on 2026/5/8.
//
#ifdef __linux__
#include "infra/socket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>  // select函数
#include <sys/time.h>    // struct timeval
#include <sys/types.h>   // fd_set类型
#include <unistd.h>      // close
#include <errno.h>       // errno
int socket_create(TransportProtocol protocol,SocketHolder socket) {
    return 0;
}

int socket_bind(SocketHolder socket,int port) {
    return 0;
}

int socket_send(SocketHolder socket,const void *buf, size_t buf_len,NetEnd dest) {
    return 0;
}


/**
 * 非阻塞接收数据
 * @param socket
 * @param buf
 * @param buf_len 缓冲区大小
 * @return 收到的数据长度，没有数据返回0，异常返回-1
 */
int socket_recv_nowait(SocketHolder socket,void *buf, size_t buf_len,const NetEnd *source) {
    return 0;
}

int socket_release(SocketHolder socket) {
    return 0;
}

#endif
