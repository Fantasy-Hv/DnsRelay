//
// Created by yian on 2026/5/8.
//
#pragma once
#include <stdint.h>
#include "infra/socket.h"
#ifndef DNSRELAY_SERVER_H
#define DNSRELAY_SERVER_H
//缓存配置项
#define KEY_CACHE_SIZE "dns_cache_size"
#define VALUE_DEFAULT_CACHE_SIZE 1024
//端口配置项
#define KEY_SERVER_PORT "server_port"
#define VALUE_DEFAULT_SERVER_PORT 53
//超时配置项,单位为秒
#define KEY_PACKET_TIMEOUT "dns_packet_timeout"
#define VALUE_DEFAULT_PACKET_TIMEOUT 5
#define SOCKET_REV_BUF_SIZE 1024
typedef enum {
    WAITING_UPSTREAM, //等待上游服务器响应
    DONE //完成，或者超时且不再重试
}SessionState;
typedef struct {
    uint16_t query_id; // 客户端请求的id
    IpAddr client_ip;
    SessionState state;
    int64_t timestamp; //上一次请求转发时的时间戳，如果请求不需要转发，为0.
    uint16_t transfer_id; // 请求转发时使用的id
    char retry_times; // 已经重试的次数
}Session;

/**
 * 启动服务器主线程
 * @return
 */
int server_start();

#endif //DNSRELAY_SERVER_H