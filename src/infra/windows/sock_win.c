//
// Created by yian on 2026/5/8.
//
#ifdef _WIN32
#include "infra/socket.h"

int socket_create(TransportProtocol protocol) {
    return 0;
}

int socket_bind(int socket_fd,int port){
    return 0;
}


int socket_send_async(int sockfd,const void *buf, size_t len){
    return 0;
}
#endif
