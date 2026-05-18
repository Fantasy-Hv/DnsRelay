//
// Created by yian on 2026/5/8.
//
#ifdef __linux__
#include <errno.h>

#include "infra/socket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>  // select函数
#include <sys/time.h>    // struct timeval
#include <sys/types.h>   // fd_set类型
#include <unistd.h>      // close
#include <stdlib.h>
#include <string.h>

#include "infra/exception.h"


int socket_create(SocketHolder *socket_holder) {
    int fd = socket(AF_INET6,SOCK_DGRAM,0);
    if (fd==-1) {
        ex_throw("syscall socket");
        return -1;
    }
    int opt = 0;
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)); //双栈模式，同时支持两个ip版本
    *socket_holder = fd;
    return 0;
}

int socket_bind(SocketHolder socket,u_int16_t port) {
    struct sockaddr_in6 addr;
    memset(&addr,0,sizeof(struct sockaddr_in6));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(port);

    int ret = bind(socket,(struct  sockaddr*)&addr,sizeof(struct sockaddr_in6));
    if (ret==-1)
         ex_throw("syscall bind");
    return ret;
}

/**
 * 发送数据
 * @param socket
 * @param buf
 * @param buf_len
 * @param dest
 * @return >0-拷贝到内核缓冲区的字节数，0-发送了0字节，-1-错误
 */
int socket_send(SocketHolder socket,const void *buf, size_t buf_len,NetEnd dest) {
    // 构造地址
    struct sockaddr* addr;
    int add_len ;
    if (dest.version==IPV4) {

        struct sockaddr_in* in_addr = malloc(sizeof(struct sockaddr_in));
        memset(in_addr,0,sizeof(struct sockaddr_in));
        in_addr->sin_family = AF_INET;
        in_addr->sin_addr.s_addr = dest.addr.ipv4; //
        in_addr->sin_port = dest.port; //NetEnd 里的地址端口都是网络序

        addr = (struct sockaddr*)in_addr;
        add_len = sizeof(struct  sockaddr_in);
    } else { //ipv6

        struct sockaddr_in6* in_addr = malloc(sizeof(struct sockaddr_in6));
        memset(in_addr,0,sizeof(struct sockaddr_in6));
        in_addr->sin6_port = dest.port;
        in_addr->sin6_family = AF_INET6;
        memcpy(in_addr->sin6_addr.__in6_u.__u6_addr8,dest.addr.ipv6,16);

        addr = (struct sockaddr*)in_addr;
        add_len = sizeof(struct sockaddr_in6);
    }
    //发包
    int ret;
    ret = sendto(socket, buf, buf_len,SOCK_NONBLOCK, addr, add_len);
    if (ret==-1)
        ex_throw("syscall sendto");
    free(addr);
    return ret;
}


/**
 * 非阻塞接收数据
 * @param socket
 * @param buf
 * @param buf_len 缓冲区大小
 * @return 收到的数据长度，没有数据返回0，其他异常，
 */
int socket_recv_nowait(SocketHolder socket,void *buf, size_t buf_len, NetEnd *source) {
    ssize_t rn = 0;
    struct sockaddr_in6 src;
    socklen_t addrlen = sizeof(src);

    //收包
    rn = recvfrom(socket,buf,buf_len,SOCK_NONBLOCK,(struct sockaddr*)&src,&addrlen);
    if (rn==-1) {
        ex_throw("syscall recvfrom");
        return rn;
    }
    // 拿地址
    if (IN6_IS_ADDR_V4MAPPED(&src.sin6_addr)) {
        memcpy(&source->addr.ipv4,&src.sin6_addr.__in6_u.__u6_addr32[3],4);
        source->port = src.sin6_port;
        source->version = IPV4;
    }else {
        memcpy(&source->addr.ipv6,src.sin6_addr.__in6_u.__u6_addr8,16);
        source->port = src.sin6_port;
        source->version = IPV6;
    }

    return rn;
}

int socket_release(SocketHolder socket) {
    close(socket);
    return 0;
}
/**
 * 系统调用select的封装
 * 阻塞当前线程，如果socket有数据或者超时，唤醒线程。
 * @param socket_holder
 * @param timeout 超时时间，如果为负数表示永不超时
 * @param socket_cnt 等待的socket数量
 * @return
 */
int socket_sleep_on(SocketHolder socket_holder,int socket_cnt,ms timeout) {
    // 准备参数
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket_holder,&set); // 设置要监听的socket集合
    struct timeval time = (struct timeval ){timeout/1000,(timeout%1000)*1000}; // 设置超时时间
    // 监听事件
    int ret =  select(socket_holder+1,&set,NULL,NULL,&time);
    // 错误记录
    if (ret<0)
       ex_throw("syscall select");
    return ret;
}
#endif
