//
// Created by yian on 2026/5/8.
//


#include "infra/config.h"
#include "infra/logger.h"
#include "infra/socket.h"
#include "server/session.h"
#include "server/server.h"

#include <stdlib.h>
#include <string.h>


#include "infra/stl.h"
#include "infra/utils.h"
#include "dns/protocol.h"
#include "dns/id.h"
#include "server/daemon.h"
#include <threads.h>

#include "infra/exception.h"
static ms request_timeout = VALUE_DEFAULT_REQUEST_TIMEOUT*1000;
static int max_retry_time = VALUE_DEFAULT_MAX_RETRY_TIME;
#define DNS_RECV_BUF_SIZE 1024
#define DNS_SEND_BUF_SIZE 1024

/**
 * 套接字引用
 */
static SocketHolder socket_holder;
/**
 * 上游服务器ip列表，类型 *T = NetEnd,port字段无意义，固定为53
 */
static LinkedList * upstreams;

/**
 * 复用的接收缓冲区
 */
static char* recv_buf;
/**
 * 从socket中非阻塞地收取一个dns包，
 * @param dns_pack 接收到的dns包
 * @param src 包的 来源
 * @return 是否读到有效包 0表示包可用，1-没有数据,-1失败,此时指针内容为NULL
 */
int pack_recv(DnsPacket** dns_pack, NetEnd *src) {

    const int len = socket_recv_nowait(socket_holder, recv_buf, DNS_RECV_BUF_SIZE,src);

    if (len == 0)
        return 1;
    if (len == -1) { // 在返回途中构建错误发生时的调用链条
        ex_throw("pack_recv");
        return -1;
    }

    return pack_deserialize(recv_buf, len, dns_pack);
}

/**
 * 复用的发送缓冲区
 */
char* send_buf;
/**
 * 将dns结构体序列化后发送到指定目标
 * @param dns_pack
 * @param dest
 * @return 0正常，-1错误
 */
int packet_send(const DnsPacket* dns_pack,const NetEnd* dest) {

    if (!dns_pack||dest==NULL) {
        ex_throw("pack send: pack null / dest null");
        return -1;
    }

   const int raw_pack_size = pack_serialize(dns_pack,send_buf,DNS_SEND_BUF_SIZE);
    if (raw_pack_size<0||raw_pack_size>MAX_PACKET_SIZE)
        return -1;

   return socket_send(socket_holder,send_buf,raw_pack_size,*dest) >=0 ? 0:-1;

}


//下一个上游服务器的索引,用于轮询式负载均衡
static struct LinkNode *next_upstream = NULL;
/**
 * 从可用服务器中选择一个,这里可以实现负载均衡策略
 * @return 上游服务器端点指针
 */
static NetEnd* pick_upstream() {
    if (next_upstream == NULL)
        next_upstream = upstreams->head;

    NetEnd *upstream = next_upstream->data;
    next_upstream = next_upstream->next;

    return upstream;
}


/**
 * 配置dns所需的特定socket
 * @return
 */
static int init_socket() {

     if (socket_create(&socket_holder)) {
         ex_throw("init_socket");
         return -1;
     }

    int port;
    if (config_get(SERV_SECTION,KEY_SERVER_PORT,(T*)&port))
        port = VALUE_DEFAULT_SERVER_PORT;

    if (socket_bind(socket_holder, port)) {
        ex_throw("init_socket");
        return -1;
    }
    return 0;
}

/** 
 * 处理超时事务，如果请求重传需要重新计时
 * 如果重试条件不通过，直接结束事务，释放事务id
 * @param session
 * @return
 */
static void do_handle_timeout(Session* session) {

    if (session->relay_info.retry_times >= max_retry_time ) {
        id_free(session->relay_info.relay_packet->header.id);
        session_close(session);
        return ;
    }

    // 再次发送
    ex_try();
    if (packet_send(session->relay_info.relay_packet, pick_upstream())==-1) {
        do_log(ERROR,"timeout resend failed %s",ex_end());
        session->relay_info.retry_times--;
    }

    session->relay_info.retry_times++;
    session_wait(session);
}

/**
 * 检查会话队列，处理超时
 */
static void batch_timeout() {
    Session *session = NULL; ms time_remain;
    while ((session = session_peek())) {

        get_session_timeout_remain(session,request_timeout, &time_remain);
        if (time_remain > 0)
            break;

        do_log(INFO,"handling timeout sess (%d-%d).",session->client_id,session->relay_info.relay_packet->header.id);

        do_handle_timeout(session);
    }
}

/**
 * 处理收到的dns包
 * @return
 */
static void handle_dns_packet(const DnsPacket *packet_in, NetEnd source_end) {
    DnsPacket *packet_out; // 临时数据包
    ex_try(); // 这里的错误不需要向上传递，自己处理

    if (packet_is_query(packet_in)) {
        //请求包
        PacketDirection direction = pack_try_response_local(packet_in, &packet_out);
        if (direction == CLIENT) {
            //本地可以直接响应
            packet_send(packet_out, &source_end);
            pack_free(packet_out);
        } // 需要转发
        else {
            //构造中继包，申请id
            uint16_t relay_id;
            if (id_alloc(&relay_id)) { // 没有id了，返回失败响应
                do_log(WARN, "server : relay id exhausted,resp fallback");
                pack_make_inner_error(packet_in,&packet_out);
                packet_send(packet_out,&source_end);
                pack_free(packet_out);
                return ;
            }
            //发送中继包
            pack_make_query_relay(packet_in, relay_id, &packet_out);
            packet_send(packet_out, pick_upstream());
            // 发不出去
            if (ex_catch()) {
                do_log(ERROR,"server:relay_query send err,%s",ex_end());
                id_free(relay_id);
                pack_free(packet_out);
                return;
            }
            // 发送成功 开启会话
            session_open(packet_in->header.id, source_end, packet_out);
            //释放临时数据
            pack_free(packet_out);
        }
    } else {
        // 响应包
        //获取对应session
        Session *session = session_get(packet_in);
        if (session) {
            //返回响应给客户端
            pack_make_response_relay(packet_in, &packet_out, session->client_id);
            packet_send(packet_out, &session->client_ip);

            if (ex_catch()) // 发不出去
                do_log(ERROR,"server:relay-response ,%s",ex_end());

            //结束会话
            session_close(session);
            id_free(packet_in->header.id);
            pack_free(packet_out);
        }
        else do_log(WARN, "server : no session match rsp, drop pack");
    }
}

static int server_loop() {
    // ReSharper disable once CppDFAEndlessLoop
    while (1) {
        // 准备select参数
        ms next_timeout;
        Session * earliest_session = session_peek();
        if (!earliest_session)
            next_timeout = -1;
        else get_session_timeout_remain(earliest_session,request_timeout,&next_timeout);

        ex_try(); // 开启错误上下文
        socket_sleep_on(socket_holder,1,next_timeout);
        if (!ex_catch()) { // 收取dns数据包，select没有错误

            DnsPacket * packet ;
            NetEnd source_end;

            while (1) {

                int ret = pack_recv(&packet,&source_end); //需要返回值控制
                if (ret==1) {
                    do_log(WARN,"server loop: no data in sock");
                    break; // 没有数据了
                }
                if (ret==-1) { // pack_recv有错误，获取上下文
                    do_log(ERROR,"server loop err: %s",ex_end());
                    break;
                }
                // 单个包处理出错对本层控制流无影响，因此不关心返回值也不关心错误，函数自己处理。
                handle_dns_packet(packet,source_end);
                pack_free(packet);
            }
            // 超时包处理，这些超时包处理出错也不影响事件循环，不关心结果。
            batch_timeout();
        }
        // select错误，获取上下文
        else {
            do_log(ERROR,"server error : %s",ex_end());
            break;
        }
    }
    return -1;
}
int server_config_parser(const char* key,const char* value,T* result) {
    if (!strcmp(key,KEY_UPSTREAMS)) { // value是上游列表
        int vl ;
        if ( (vl = strlen(value)+1) == 1) return 0; // value “”

        int i=0,j = 0; //滑动窗口枚举每个ip串 i指向第一个有效字符，j指向最后一个字符，ip字符串[i,j)
        char item[64];
        LinkedList* ups = linked_list_create();

        if (value[j] == ',')i=++j; //  第一个"，"被跳过
        for (; j<vl; ++j) {

            if (value[j]==','||value[j]=='\0') {

                memcpy(item,&value[i],j-i);
                item[j-i]='\0';
                NetEnd* end ;
                if (ipstr2binary(item,&end)) {  // 解析失败
                    ex_throw("serv_config_parser:upstream failed");
                    return -1;
                }
                linked_list_addFirst(ups,end);
                i=j+1;
            }
        }
        *result = ups;
        return 0;
    }
    if (!strcmp(key,KEY_MAX_RETRY_TIME)) {
        *result = (T)atol(value);   // atol : 字符串转long
        return 0;
    }
    if (!strcmp(key,KEY_PACKET_TIMEOUT)) {
        *result = (T)(atol(value)*1000);
        return 0;
    }
    // 降级为原字符串
    return -1;
}

/**
 * 解析值清理函数
 * @param key
 * @param value
 */
void server_config_cleaner(const char * key,T value) {
    if (!strcmp(key,KEY_UPSTREAMS)) {
        LinkedList * ups = value;
        linked_list_foreach(ups,sock_config_free_netend);
    }
}


int server_start() {
    ex_try();
    // 注册到配置系统
    config_register_parser(SERV_SECTION,server_config_parser);
    config_register_cleaner(SERV_SECTION,server_config_cleaner);

    //初始化降级策略配置
    config_get(SERV_SECTION,KEY_PACKET_TIMEOUT,(T*)&request_timeout);
    config_get(SERV_SECTION,KEY_MAX_RETRY_TIME,(T*)&max_retry_time);
    if (ex_catch())do_log(ERROR,"serv_config read failed %s",ex_end());

    //获取上游服务器列表
    config_get(SERV_SECTION,KEY_UPSTREAMS,(T*)&upstreams);
    if (linked_list_is_empty(upstreams)) {
        do_log(ERROR,"server:upstream not configured %s",ex_end());
        return -1;
    }

    //创建守护线程
    thrd_t cache_ttl;
    thrd_create(&cache_ttl,daemon_dnscache_ttl,NULL);
    thrd_detach(cache_ttl);

    //初始化socket
    ex_try();
    if (init_socket()) {
        do_log(ERROR,"server start: %s",ex_end());
        return -1;
    }

    //进入主循环,处理请求
    recv_buf = malloc(DNS_RECV_BUF_SIZE);
    send_buf = malloc(DNS_SEND_BUF_SIZE);
    return server_loop();
}

