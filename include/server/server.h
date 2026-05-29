//
// Created by yian on 2026/5/8.
//


#ifndef DNSRELAY_SERVER_H
#define DNSRELAY_SERVER_H

#define SERV_SECTION "server"

//超时配置项,单位为秒
#define KEY_PACKET_TIMEOUT "dns_packet_timeout"
#define VALUE_DEFAULT_REQUEST_TIMEOUT 3

//最大重试次数
#define KEY_MAX_RETRY_TIME "max_retry_time"
#define VALUE_DEFAULT_MAX_RETRY_TIME 2
//上游dns服务器ip,多个用逗号隔开
#define KEY_UPSTREAMS "server_upstream"
#define SERVER_PORT 53
/**
 * 启动服务器主线程
 * @return
 */
int server_start();

#endif //DNSRELAY_SERVER_H