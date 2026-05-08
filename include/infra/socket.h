//
// Created by yian on 2026/5/8.
//
#pragma once

#ifndef DNSRELAY_SOCKET_H
//linux依赖
#include <sys/socket.h>
#include <sys/types.h>          // 基本数据类型定义
// windows依赖
#define DNSRELAY_SOCKET_H

int create_socket(int protocol);

int bind_port(int socket_fd,int port);

int send_async(int sockfd,const void *buf, size_t len);


#endif //DNSRELAY_SOCKET_H