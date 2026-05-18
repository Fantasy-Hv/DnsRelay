//
// Created by yian on 2026/5/9.
//
#ifndef DNSRELAY_SESSION_H
#define DNSRELAY_SESSION_H
#include "dns/protocol.h"
#include <stdint.h>

#include "infra/socket.h"
#include "infra/sys.h"


typedef struct {
    ms timestamp; //上一次请求转发时的时间戳，如果请求不需要转发，为0.
    char retry_times; // 已经重试的次数
    DnsPacket* relay_packet; // 发送包的缓存
}RelayInfo;
typedef struct {
    uint16_t client_id; // 客户端请求的id
    NetEnd client_ip;
    RelayInfo relay_info;
}Session;
/**
 * 初始化会话存储，不要重复调用
 */
int session_factory_init();
/**
 * 根据返回的dns响应包获取对应会话
 * @param relay_response
 * @return
 */
Session * session_get(const DnsPacket* relay_response);

/**
 * 为客户端id开启一个会话，并添加到等待队列
 * @param client_id 客户端请求的id
 * @param client_ip 客户端位置
 * @param relay_pack 缓存的中继包
 * @return
 */
int session_open(uint16_t client_id,NetEnd client_ip,const DnsPacket * relay_pack);

/**
 * 关闭会话
 * @param session
 */
void session_close(Session *session);


/**
 * 查看剩余时间最小的会话
 * @return 如果没有会话在等待，返回NULL
 */
Session* session_peek();


/**
 * 启动会话的超时定时器
 * @param session
 * @return
 */
int session_wait(Session* session) ;

/**
 * 获取会话的超时剩余时间
 * @param session not null
 * @param timeval 剩余时间
 * @return 0正常，-1表示异常，返回的timeval值无效
 * */
int get_session_timeout_remain(const Session *session,ms timeout,ms* timeval) ;

char* session_to_log_str(const Session *session);
#endif //DNSRELAY_SESSION_H