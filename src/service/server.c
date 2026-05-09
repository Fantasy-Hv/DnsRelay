//
// Created by yian on 2026/5/8.
//
#include "dns/server.h"

#include <stdlib.h>

#include "dns/cache.h"
#include "dns/config.h"
#include "infra/logger.h"
#include "infra/socket.h"
//跨平台条件编译
#ifdef __linux__
#include <sys/select.h>  // select函数
#include <sys/time.h>    // struct timeval
#include <sys/types.h>   // fd_set类型
#include <unistd.h>      // close
#include <errno.h>       // errno
#endif

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>    // select函数和Winsock API
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")  // 链接库
#endif
#include "infra/stl.h"
#include "infra/sys.h"
#include "dns/protocol.h"
static long packet_timeout = 0;
static SocketHolder socket_holder;
//
static HashMap *sessions_map; // 用于查询
static PriorityQueue *sessions_queue; //用于超时管理

Session * create_session(uint16_t client_id) {
    Session* session = malloc(sizeof(Session));
    session->timestamp = 0;
    session->retry_times = 0;
    session->client_id = client_id;
    return session;
}
int get_time_remain(const Session *session,struct timeval * timeval) {
    int64_t diff = sys_time_ms() - session->timestamp;
    timeval->tv_sec  =  diff/ 1000;
    timeval->tv_usec = (diff % 1000) * 1000;
    return 0;
}


/** todo
 * 处理超时会话，如果请求重传需要重新计时
 * @param session
 * @return
 */
int handle_timeout(Session* session) {

    return 0;
}

/**
 * 请求会话比较函数，用于给会话排序
 * @param a
 * @param b
 * @return
 */
int session_comparator(void* a, void* b) {
    //时间早的放前面
    long long interval = ((Session*)a)->timestamp - ((Session*)b)->timestamp;
    if (interval>0)return 1;
    if (interval<0)return -1;
    return 0;
}

/**
 * 配置dns所需的特定socket
 * @return
 */
int init_socket() {
     if (socket_create(UDP,&socket_holder))
        return 1;
    int port;
    if (get_property(KEY_SERVER_PORT,&port))
        port = VALUE_DEFAULT_SERVER_PORT;
    socket_bind(socket_holder, port);
    return 0;
}

/**
 * 检查会话队列，处理超时会话
 */
void check_timeout() {
    Session *session = priority_queue_peek(sessions_queue);
    if (!session)return ;
    do {
        struct timeval timeout;
        get_time_remain(session,&timeout);
        if (timeout.tv_sec > 0 || timeout.tv_usec > 0)
            return ;
        handle_timeout(session);
        session = priority_queue_peek(sessions_queue);
    }while (session);
}

int handle_dns_packet(const DnsPacket*packet) {
    //todo 绑定会话
    //
    // make_request(packet,socket_holder);
    return 0;
}
int server_loop() {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(*(int*)socket_holder, &readfds);

    while (1) {
        Session * earliest_session = priority_queue_peek(sessions_queue); //fixme 可能为NULL！
        struct timeval next_timeout;
        get_time_remain(earliest_session,&next_timeout);

        int stat =select(*(int*)socket_holder+1,&readfds, NULL, NULL, &next_timeout);

        if (stat>0) {
            // 接收dns数据包
            char* buf = malloc(SOCKET_REV_BUF_SIZE);
            int rev_cnt = 0;
            DnsPacket * packet = malloc(sizeof(DnsPacket));
            while (( rev_cnt =  socket_recv_nowait(socket_holder,buf,SOCKET_REV_BUF_SIZE))>0){
                if (deserialize_reuse(buf,rev_cnt,packet))
                    do_log(ERROR,"dns packet deserialize error");
                do_log(DEBUG,to_log_string_packet(packet));
                //todo 处理dns报文以及会话管理
                handle_dns_packet(packet);
            }

        }
        //todo 响应会话超时

    }

    return 0;
}
int server_start() {
    //初始化缓存
    int cache_size ;
    if (get_property(KEY_CACHE_SIZE, &cache_size))
        cache_size = VALUE_DEFAULT_CACHE_SIZE;
    cache_init(cache_size);
    //todo 创建守护线程

    //创建会话队列
    sessions_queue = priority_queue_create(session_comparator);
    //初始化socket
    if (init_socket())
        return 1;
    //进入主循环
    server_loop();
    return 0;
}


