//
// Created by yian on 2026/5/9.
//
#include <stdlib.h>
#include <string.h>

#include "infra/logger.h"
#include "server/session.h"
#include "infra/stl.h"
#include "infra/socket.h"
#include "infra/utils.h"


static HashMap *agent_id_sessions; // 用于根据上游请求获取会话
static PriorityQueue *sessions_queue; //用于超时管理

/**
 * 会话比较函数，用于给会话排序、判等
 * todo fixme
 * @param a
 * @param b
 * @return
 */
static int session_comparator(void* a, void* b) {
    //时间早的放前面
    if (a==NULL)return -1;
    if (b==NULL)return -1;
    Session* a_session = a;
    Session* b_session = b;
    long long interval = a_session->relay_info.timestamp - b_session->relay_info.timestamp;
    if (interval>0)return 1;
    if (interval<0)return -1;
    //先按时间排序，如果时间不一样那就是不一样的条目
    if (a_session == b_session)return 0;
    return 0;
}


int session_factory_init() {
    sessions_queue = priority_queue_create(session_comparator);
    agent_id_sessions = hash_map_create(hash_uint16_t,compare_uint16);
    return !sessions_queue || !agent_id_sessions;
}




Session * session_get(const DnsPacket* relay_response) {
    if (!relay_response)return NULL;
    uint16_t id = relay_response->header.id;
    T ses ;
    if (hash_map_get(agent_id_sessions,(K)id,&ses))
        return NULL;
    return ses;
}
void session_id_key_destructor(K key) {
    // 因为key是纯数字，不是指针，所以不需要释放
}
/**
 * 关闭中继请求的会话
 * @param session
 */
 void session_close(Session *session) {
    do_log(DEBUG,"ses close for cli-%d,reid-%d",session->client_id,session->relay_info.relay_packet->header.id);
    K key = (K)session->relay_info.relay_packet->header.id; // relay_id
    hash_map_remove(agent_id_sessions,key,session_id_key_destructor);
    //可以释放，因为队列里实际上存的是指针而不是会话，
    priority_remove(sessions_queue,session);
    pack_free(session->relay_info.relay_packet);
    free(session);
}

/**
 * 会话的超时剩余时间
 * @param session not null
 * @param timeval 剩余时间
 * @return 0正常，-1表示异常，返回的timeval值无效
 * */
 int get_session_timeout_remain(const Session *session,ms timeout,ms *timeval) {
    if (session==NULL)return -1;
    const ms elapse = sys_time_ms() - session->relay_info.timestamp;
    *timeval = timeout - elapse;
    return 0;
}




/**
 * 查看超时剩余时间最小的会话
 * @return
 */
Session* session_peek() {
    return  priority_queue_peek(sessions_queue);
}

/**
 * 将会话挂起至等待队列
 * @param session
 * @return
 */
int session_wait(Session *session){
    session->relay_info.timestamp = sys_time_ms();
    //直接修改堆元素的排序key,堆无法感知元素变化，这会破坏堆结构
    //因此需要将原来的元素删除后重新添加，以此维护正确的大小顺序。
    //删除原有引用
    priority_remove(sessions_queue,session);
    //添加新引用
    priority_queue_add(sessions_queue,session);
    return 0;
}

/**
 * 开启一个会话,并添加到等待队列
 * @param client_id
 * @param client_ip
 * @param relay_pack
 * @return
 */
int session_open(uint16_t client_id,NetEnd client_ip,const DnsPacket * relay_pack) {
    Session* session = malloc(sizeof(Session));
    do_log(DEBUG,"sesionopen for cli-%d,reid-%d",client_id,relay_pack->header.id);
    session->client_id = client_id;
    session->client_ip = client_ip;
    session->relay_info.retry_times = 0;
    session->relay_info.relay_packet = packet_clone(relay_pack);
    hash_map_put(agent_id_sessions,(K)session->relay_info.relay_packet->header.id,session);
    session_wait(session);
    return 0;
}

char* session_to_log_str(const Session *session) {
    return NULL;
}

