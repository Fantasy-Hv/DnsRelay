//
// Created by yian on 2026/5/8.
//


#include "dns/cache.h"
#include "infra/config.h"
#include "infra/logger.h"
#include "infra/socket.h"
#include "server/session.h"
#include "server/server.h"
#include "infra/thread.h"
#include "infra/stl.h"
#include "infra/sys.h"
#include "dns/protocol.h"
#include "dns/id.h"
#include "server/daemon.h"
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
 * @return 是否读到有效包 0表示包可用，-1表示没有数据或者包解析失败,此时指针内容为NULL
 */
int pack_recv(DnsPacket** dns_pack, NetEnd *src) {
    const int len = socket_recv_nowait(socket_holder, recv_buf, DNS_RECV_BUF_SIZE,src);
    if (len <= 0) return -1;
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
 */
void packet_send(const DnsPacket* dns_pack,const NetEnd* dest) {
   const int raw_pack_size = pack_serialize(dns_pack,send_buf);
   socket_send(socket_holder,send_buf,raw_pack_size,*dest) ;
}


//下一个上游服务器的索引,用于轮询式负载均衡
static struct LinkNode *next_upstream = NULL;
/**
 * 从可用服务器中选择一个,这里可以实现负载均衡策略
 * @return 上游服务器端点指针，如果没有可用的上游服务器，返回NULL
 */
static NetEnd* pick_upstream() {
    if (next_upstream == NULL) next_upstream = upstreams->head;
    NetEnd *upstream = next_upstream->data;
    next_upstream = next_upstream->next;
    return upstream;
}


/**
 * 配置dns所需的特定socket
 * @return
 */
static int init_socket() {
     if (socket_create(UDP,&socket_holder))
        return -1;
    int port;
    if (config_get(KEY_SERVER_PORT,&port))
        port = VALUE_DEFAULT_SERVER_PORT;
    if (socket_bind(socket_holder, port))
        return -1;
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
    packet_send(session->relay_info.relay_packet, pick_upstream());
    session->relay_info.retry_times++;
    session_wait(session);
}

/**
 * 检查会话队列，处理超时
 */
static void batch_timeout() {
    Session *session;ms time_remain;
    while ((session = session_peek())) {
        if (get_session_timeout_remain(session,request_timeout, &time_remain)) {
            do_log(ERROR,"server:ses timeout not found %s",session_to_log_str(session));
            break;
        }
        if (time_remain > 0)
            break;
        do_handle_timeout(session);
    }
}

/**
 * 处理收到的dns包
 * @param packet_in 收到的dns包
 * @param source_end 包的来源
 * @return
 */
static int handle_dns_packet(const DnsPacket*packet_in,NetEnd source_end) {
    DnsPacket* packet_out ;
    if (packet_is_query(packet_in)) { //请求包
        PacketDirection direction = pack_make_local_answer(packet_in,&packet_out);
        if (direction==CLIENT) { //本地可以直接响应
            packet_send(packet_out,&source_end);
             pack_free(packet_out);
        } // 需要转发
        else if (!linked_list_is_empty(upstreams)) {
            //申请id
            uint16_t relay_id;
            if (id_alloc(&relay_id)) {
                //id 不足 ,返回失败响应
                do_log(WARN,"server:id alloc failed");
                pack_make_inner_error(packet_in,&packet_out);
                packet_send(packet_out,&source_end);
                pack_free(packet_out);
            } else {
                pack_make_relay(packet_in,relay_id,&packet_out);
                //发送中继包
                packet_send(packet_out, pick_upstream());
                // 开启会话，将该包存储在会话中
                session_open(packet_in->header.id,source_end,packet_out);
            }
        }
        else do_log(WARN,"server : no upstream found ");

    }else { // 响应包
        //获取对应session
        Session * session = session_get(packet_in->header.id);
        if (session) { //发送给客户端
            pack_make_response_relay(packet_in,&packet_out,session->client_id);
            packet_send(packet_out,&session->client_ip);
            //结束会话
            session_close(session);
            // 回收id
            id_free(packet_in->header.id);
        }
        else do_log(WARN,"server : no session match rsp, drop pack");
    }
    return 0;
}

static void server_loop() {
    // ReSharper disable once CppDFAEndlessLoop
    while (1) {
        // 准备select参数
        Session * earliest_session = session_peek();
        ms next_timeout;
        if (!earliest_session)
            next_timeout = -1;
        else get_session_timeout_remain(earliest_session,request_timeout,&next_timeout);

        const int stat = socket_sleep_on(&socket_holder,1,next_timeout);
        if (stat>=0) { // 收取dns数据包
            DnsPacket * packet ;NetEnd source_end;
            while (!pack_recv(&packet,&source_end)) {
                handle_dns_packet(packet,source_end);
                pack_free(packet);
            }
            batch_timeout();
        }
        // 错误
        else {
            do_log(ERROR,"socket_sleep_on error : %s",get_syscall_error().msg);
            return;
        }
    }
}

int server_start() {
    //初始化降级策略配置
    config_get(KEY_PACKET_TIMEOUT,&request_timeout);
    config_get(KEY_MAX_RETRY_TIME,&max_retry_time);
    //获取上游服务器列表
    upstreams = linked_list_create();
    config_get(KEY_UPSTREAMS,upstreams);
    if (linked_list_is_empty(upstreams))
        do_log(WARN,"server:upstream not configured");
    //创建守护线程
    Thread thread ;
    thread_create(&thread,daemon_dnscache_ttl,NULL); //定时清理缓存
    //初始化socket
    if (init_socket()) {
        do_log(ERROR,"server : socket init failed");
        return 1;
    }
    //初始化session工厂
    session_factory_init();
    //进入主循环,处理请求
    server_loop();

    return 0;
}


