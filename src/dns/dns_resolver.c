//
// Created by yian on 2026/5/9.
//
// 核心模块！！！其他模块可以没有，但是这个必须很完善。


#include "dns/protocol.h"
#include "dns/cache.h"
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

#include "infra/exception.h"
#include "infra/utils.h"
#define DNS_REV_BUF_SIZE 1024
/**
 *目前需要满足的两类需求：
 一、dns包的序列化和反序列化
  1.序列化dns包
 * header可以直接强转然后转换字节序
 * question节需要根据header的qdcont循环解析
    每个question解析行为是固定的，name字段自解码，qtype和qclass定长需要转换字节序
  RR的解析：分别按照header中指定的数量来循环解析每条RR
  rr的解析，前面ttl、type、class、data length都是定长，需要转换字节序，
  name需要解码，然后rdata直接copy就行了。
  总的来说没有复杂的逻辑
 *2.反序列化dns包
    header需要转换字节序，然后copy
    question需要循环列表，编码name,qtype、qclass转换字节序
    rr一样，定长字段转换字节序，name编码，rdata直接copy
 二、dns包内容解析，本质上是要回答：如何从一个包构造另一个包，需要仔细研究协议规定的行为
    1.header中flags的解析，需要根据值采取相应的行为
    2.question中内容的解析，
 */


/**
 * 将人类可读的域名字符串转为dns编码（非指针）
 * @decrepted 貌似没必要转换～
 * @return
 */
char *encode_name(const char *) {
    return NULL;
}

/**
 * 将dns包的域名字段解析为人类可读的字符串
 * @decrepted 貌似没必要转换～
 * @return
 */
char *decode_name(const char *) {
    return NULL;
}


static void header_endian_2h(SectionHeader *section_header) {
    uint16_t *cursor = (uint16_t *) section_header;
    for (int i = 0; i < 6; i++, cursor++)
        n2h_2(cursor);
}

static void header_endian_2n(SectionHeader *section_header) {
    uint16_t *cursor = (uint16_t *) section_header;
    for (int i = 0; i < 6; i++, cursor++)
        h2n_2(cursor);
}


ResourceRecord *rr_create() {
    ResourceRecord *rr = malloc(sizeof(ResourceRecord));
    rr->name = rr->rdata = NULL;
    rr->rdata_length = 0;
    return rr;
}

void rr_free(ResourceRecord *rr) {
    free(rr->rdata); //这是安全的，通过malloc分配的块大小会被追踪，整个内存块都可以被顺利释放
    free(rr->name);
    free(rr);
}

SectionQuestion *question_create() {
    SectionQuestion *q = malloc(sizeof(SectionQuestion));
    q->qname = NULL;
    return q;
}

void question_free(SectionQuestion *q) {
    free(q->qname);
    free(q);
}
//释放列表中RR的内存,连同列表也一起释放
void free_questions(Vector* questions) {
    for (int i = 0; i < vector_size(questions); i++)
        question_free(vector_get(questions, i));
    vector_free(questions);
}

/**
 * 释放列表中RR的内存,连同列表也一起释放
 * @param vector
 */
void free_rrs(Vector *vector) {
    for (int i = 0; i < vector_size(vector); i++)
        rr_free(vector_get(vector, i));
    vector_free(vector);
}

/**
 * @param dns_pack 释放该dns包占用的内存
 */
void pack_free(DnsPacket *dns_pack) {
    //1.需要释放三个段
    // 包结构体 <-- 三个列表 <-- 列表的所有RR <-- RR的所有指针
    // 一个包，不管逻辑上有没有这4个段的数据，都会有这些列表
    free_questions(dns_pack->questions);
    free_rrs(dns_pack->answers);
    free_rrs(dns_pack->authorites);
    free_rrs(dns_pack->additionals);
    free(dns_pack);
}
SectionQuestion* question_clone(const SectionQuestion*orig) {
    SectionQuestion* q = question_create();
    q->qname = strdup(orig->qname);
    q->qtype = orig->qtype;
    q->qclass = orig->qclass;
    return q;
}
Vector* questions_clone(Vector*questions) {
    Vector* dup = vector_create(vector_size(questions));
    for (int i = 0;i<vector_size(questions);i++)
        vector_add(dup,question_clone(vector_get(questions,i)));
    return dup;
}
/**
 * 创建一个空dns包，主要是为了初始化RR列表
 * @return
 */
DnsPacket *pack_create() {
    DnsPacket *pack = malloc(sizeof(DnsPacket));
    memset(&pack->header,0,sizeof(SectionHeader));
    pack->questions = vector_create(5);
    pack->answers = vector_create(5);
    pack->authorites = vector_create(5);
    pack->additionals = vector_create(5);
    return pack;
}

/**
 * 完备地克隆一个RR列表，
 * @param RRS
 * @return
 */
static Vector *rrs_clone(Vector *RRS) {
    Vector *clony = vector_create(vector_size(RRS));
    for (int i = 0; i < vector_size(RRS); i++) {
        const ResourceRecord *src = vector_get(RRS, i);
        ResourceRecord *item = malloc(sizeof(ResourceRecord));
        memcpy(item, src, sizeof(ResourceRecord));
        item->name = strdup(src->name);
        item->rdata = malloc(src->rdata_length);
        memcpy(item->rdata, src->rdata, src->rdata_length);
        vector_add(clony, item);
    }
    return clony;
}

DnsPacket *packet_clone(const DnsPacket *source) {
    DnsPacket *packet = pack_create();
    packet->header = source->header;
    //复制问题段
    packet->questions = questions_clone(source->questions);
    packet->answers = rrs_clone(source->answers);
    packet->authorites = rrs_clone(source->authorites);
    packet->additionals = rrs_clone(source->additionals);
    return packet;
}

/**
 *
 * @param cur_p question的起始字节
 * @param question 问题段
 * @return offset:该问题段的字节长度, -1表示异常
 */
static int question_deserialize(const char *cur_p, SectionQuestion *question) {
    //name,正常dns编码，不会出现指针
    const char *cursor = cur_p;
    question->qname = strdup(cur_p); //dns编码域名是合规的c字符串。
    cursor += strlen(question->qname) + 1; //strlen不算\0
    // type
    memcpy(&question->qtype, cursor, 2);
    n2h_2(&question->qtype);
    cursor += 2;
    //uint16 qclass
    memcpy(&question->qclass, cursor, 2);
    n2h_2(&question->qclass);
    cursor += 2;
    return cursor - cur_p;
}

/**
 * 将问题节序列化到指定起始字节开始的位置
 * @param question
 * @param cur_p 起始位置
 * @return 该节的长度
 */
static int question_serialize(const SectionQuestion* const question, char* cur_p) {
    char* cursor = cur_p;
    // qname
    strcpy(cursor,question->qname);
    cursor += strlen(question->qname) + 1; //strlen不算\0
    // type
    memcpy( cursor,&question->qtype, 2);
    h2n_2((uint16_t*)cursor);
    cursor += 2;
    //uint16 qclass
    memcpy( cursor, &question->qclass,2);
    h2n_2((uint16_t*)cursor);
    cursor += 2;
    return cursor - cur_p;
}

/**
 *
 * @param segment
 * @param qc
 * @param cur_p
 * @return
 */
static int segment_question_serialize( Vector * const segment,int qc, char* const cur_p) {
    char * cursor = cur_p;
    for (int i=0;i<qc;i++) {
        cursor += question_serialize(vector_get(segment,i),cursor);
    }
    return cursor-cur_p;
}
/**
 *
 * @param cur_p 问题段的起始字节
 * @param qc 问题个数
 * @param segment 存储列表
 * @return 问题段长度
 */
static  int segment_question_deserialize(const char *cur_p, int qc, Vector * const segment) {
    const char* cursor = cur_p;
    for (int i=0;i<qc;i++) {
        SectionQuestion * question = question_create();
        cursor += question_deserialize(cursor,question);
        vector_add(segment,question);
    }
    return cursor-cur_p;
}

/**
 * uint_16 ttl
 *
 * @param start_p dns报文起始字节
 * @param cur_p 当前待解析的rr的起始字节
 * @param rr 解析完成的rr
 * @return offset:该rr的字节长度
 */
int rr_deserialize(const char *start_p, const char *cur_p, ResourceRecord * const rr) {
    // cur_p ： 指向下一个待解析的字节
    //name
    if ((*cur_p & 0xc0) == 0xc0) {
        //值为指针，两字节
        uint16_t offset;
        memcpy(&offset, cur_p, 2);
        n2h_2(&offset);
        offset &= 0x3fff;
        const char *reloc = offset + start_p;
        rr->name = strdup(reloc); // 保留dns域名编码
        cur_p += 2;
    } else {
        rr->name = strdup(cur_p);
        cur_p += strlen(rr->name) + 1;
    }
    //type
    memcpy(&rr->type, cur_p, 2);
    n2h_2(&rr->type);
    cur_p += 2;
    //class
    memcpy(&rr->class, cur_p, 2);
    n2h_2(&rr->class);
    cur_p += 2;
    // uint32  ttl
    memcpy(&rr->ttl, cur_p, 4);
    n2h_4(&rr->ttl);
    cur_p += 4;
    //2B rdata_length
    memcpy(&rr->rdata_length, cur_p, 2);
    n2h_2(&rr->rdata_length);
    cur_p += 2;
    // rdata
    rr->rdata = malloc(rr->rdata_length);
    memcpy(rr->rdata,cur_p,rr->rdata_length);
    cur_p+=rr->rdata_length;
    return cur_p-start_p;
}

int segment_rr_deserialize(const char *start_p, const char *cur_p, int rrs, Vector * const segment) {
    const char *cursor = cur_p;
    for (int i = 0; i < rrs; i++) {
        ResourceRecord *rr = rr_create();
        cursor += rr_deserialize(start_p, cursor, rr);
        vector_add(segment, rr);
    }
    return cursor - cur_p;
}

/**
 * 在左闭右开区间搜索字符串
 * @param start_i
 * @param end_e
 * @param name
 * @return  相对起始位置的偏移量
 */
static int find_previous_name(const char* start_i,const char * end_e,const char * name) {
    const char* j = start_i;
    while (!strcmp(j,name)&&j<end_e)
        j++;
    // str_j == name || j=end_e
    return j-start_i;
}

/**
 *
 * @param start_p 整个缓冲区起始位置
 * @param cur_p 当前记录起始位置
 * @param rr
 * @return
 */
static int rr_serialize( char *start_p,  char *cur_p,const ResourceRecord * const rr) {
     char * cursor = cur_p;
     //name,尝试指针压缩
     //直接从header之后开始搜索，提高效率
     uint16_t offset = find_previous_name(start_p+sizeof(SectionHeader),cur_p,rr->name);
     if (offset < cur_p-start_p) { // 搜索到了
        offset=(offset+sizeof(SectionHeader))|0xC000; //设置高两位1,表示这是压缩指针
        memcpy(cursor,&offset,2);
        h2n_2((uint16_t*)&cursor);
        cursor+=2;
    }else { //前面没有出现，只能保留原串了。
        strcpy(cursor,rr->name);
        cursor += strlen(rr->name)+1;
    }
    //type
    memcpy(cursor,&rr->type,2);
    h2n_2((uint16_t*)&cursor);
    cursor+=2;
    //class
    memcpy(cursor,&rr->class,2);
    h2n_2((uint16_t*)&cursor);
    cursor+=2;
    //ttl
    memcpy(cursor,&rr->ttl,4);
    h2n_4((uint32_t*)&cursor);
    cursor+=4;
    //len
    memcpy(cursor,&rr->rdata_length,2);
    h2n_2((uint16_t*)&cursor);
    cursor+=2;
    // rdata
    memcpy(cursor,rr->rdata,rr->rdata_length);
    cursor+= rr->rdata_length;
    return cursor-cur_p;
}

/**
 * 在指定位置开始序列化 某个RR记录段
 * @param start_p 整个包的起始字节
 * @param cur_p 起始位置
 * @param segment rr列表
 * @param cnt rr数量
 * @return
 */
static int segment_rr_serialize(Vector * const segment,int cnt,char *start_p,  char *cur_p) {
     char * cursor = cur_p;
    for (int i = 0;i<cnt;i++) {
        cursor += rr_serialize(start_p,cursor,vector_get(segment,i));
    }
    return cursor-cur_p;
}
/**
 * @brief 将dns包结构序列化为网络字节序的字节流
 * @param dns_pack 要序列化的dns包
 * @param packet_buf 序列化缓冲区，必须预先分配足够的内存！
 * @return 序列化后的dns包大小，异常返回-1,
 */
int pack_serialize(const DnsPacket *dns_pack, char *const packet_buf) {
    char *cursor = packet_buf;
    // header
    memcpy(cursor, &dns_pack->header, sizeof(SectionHeader));
    header_endian_2n((SectionHeader *) &cursor);
    cursor += sizeof(SectionHeader);
    //question
    cursor += segment_question_serialize(dns_pack->questions, dns_pack->header.qcount, cursor);
    //answer
    cursor += segment_rr_serialize(dns_pack->answers, dns_pack->header.answer_RRs, packet_buf, cursor);
    //auth
    cursor += segment_rr_serialize(dns_pack->authorites, dns_pack->header.authority_RRs, packet_buf, cursor);
    //add
    cursor += segment_rr_serialize(dns_pack->additionals, dns_pack->header.additional_RRs, packet_buf, cursor);
    return cursor - packet_buf;
}

/**
 * @brief 将网络字节流反序列化为dns包
 * @param raw_pack 网络字节流
 * @param packet
 * @return 0-解析成功，-1解析失败
 */
int pack_deserialize(const char *raw_pack, int len, DnsPacket **packet) {
    if (len > 512) {
        ex_throw("pack_deser:size exceeded");
        return -1;
    }
    const char *cursor = raw_pack;
    DnsPacket *pac = pack_create();
    // header
    SectionHeader *header_in_buf = (SectionHeader *) raw_pack;
    memcpy(&pac->header, header_in_buf, sizeof(SectionHeader));
    header_endian_2h(&pac->header);
    cursor += sizeof(SectionHeader);
    // questions
    cursor += segment_question_deserialize(cursor,pac->header.qcount,pac->questions);
    // answer
    cursor += segment_rr_deserialize(raw_pack, cursor, pac->header.answer_RRs, pac->answers);
    // authority
    cursor += segment_rr_deserialize(raw_pack, cursor, pac->header.authority_RRs, pac->authorites);
    //additional
    cursor += segment_rr_deserialize(raw_pack, cursor, pac->header.additional_RRs, pac->additionals);
    *packet = pac;
    return 0;
}

/**
 * 将数据包转为字符串方便日志格式化输出，结尾带\0
 * @param dns_pack dns包
 * @return 字符串地址
 * 低优先级
 */
char *packet_to_log_string(const DnsPacket *dns_pack) {
    return NULL;
}

/**
 * 包是否为请求包
 * @param packet
 * @return 是-1,否-0
 */
int packet_is_query(const DnsPacket *packet) {
    return IS_QUERY(packet->header.flags);
}

/**todo
 * QR=Q
 * Opcode=Status时调用该方法生成响应包
 * @param query
 * @param response
 * @return
 */
static int make_response_status(const DnsPacket *query, DnsPacket **response) {
    return 0;
}
static  int setRcode(DnsPacket* pack,Rcode rcode) {
    if (!pack)return -1;
    // 16 位，设置低4位，
    pack->header.flags &= 0xfff0; // 全部清零
    pack->header.flags |= 0x000f&rcode; //  直接覆盖
    return 0;
}
/**
 * 构造空响应，用于标准查询下RD=0且没有缓存的情况。
 *
 * @return
 */
static int make_response_empty(const DnsPacket *query, DnsPacket **empty_response) {
    DnsPacket *response = pack_create();
    response->header = query->header;
    RA_SET(response->header.flags);
    setRcode(response,RCODE_NOERROR);
    response->header.answer_RRs = 0;
    response->header.additional_RRs = 0;
    response->header.authority_RRs = 0;
    response->questions = questions_clone(query->questions);
    *empty_response = response;
    return 0;
}

/**
 * 根据失败类型构造失败响应包
 * @param query
 * @param fail
 * @param rcode
 * @return
 */
static int make_response_fail(const DnsPacket *query, DnsPacket **fail, Rcode rcode) {
    make_response_empty(query,fail);
    setRcode(*fail,rcode);
    return 0;
}

/**
 根据qtype和qname判断此请求是否要放行,可以用来封禁域名(ext)
@param pack
@param rcode 响应状态，如果为NOERROR即为放行请求。其他情况为拒绝请求
 * @return
 */
static void query_pre_validate(const DnsPacket *pack, Rcode *rcode) {
    *rcode = RCODE_NOERROR;
}
// 检查该块内存区域是否全0
static int memallz(char* mem,int len) {
    while (len>0&&mem[--len]==0);
    return !len;
}
/**
 * 对构建好的请求进行检查，可以用来封禁ip
 * @param response
 * @param rcode
 * @return
 */
static void query_post_validate(const DnsPacket * response,Rcode*rcode) {
    //检查ans，如果ip有0.0.0.0返回NXDMAIN
    Vector * ans = response->answers;
    for (int i=0;i<vector_size(ans);i++) {
        ResourceRecord* rr = vector_get(ans,i);
        if ((rr->type==QTYPE_A||rr->type==QTYPE_AAAA)&&memallz(rr->rdata,rr->rdata_length)) {
            *rcode = RCODE_NXDOMAIN;
            return;
        }
    }
}

/**
 * 将下游的查询包转为发给上游的查询包
 * @param query_pack 客户端请求
 * @param relay_id 转发包使用的id
 * @param relay_pack 生成的转发包
 * @return
 */
int pack_make_query_relay(const DnsPacket *query_pack, uint16_t relay_id, DnsPacket **relay_pack) {
    *relay_pack = packet_clone(query_pack);
    (*relay_pack)->header.id = relay_id;
    return 0;
}


/**
 * 根据的上游响应构造下游响应。
 * @param recv 上游应答
 * @param send 返回给客户端的响应
 * @param client_id 客户端查询请求的id
 */
void pack_make_response_relay(const DnsPacket *recv, DnsPacket **send, uint16_t client_id) {
    * send = packet_clone(recv);
    (*send)->header.id = client_id;
    Vector* rrs = recv->answers;
    // 缓存资源记录
    for (int i=0;i<vector_size(rrs);i++) {
        ResourceRecord * rr = vector_get(rrs,i);
        if (rr->ttl>0)
            dns_cache_put(rr);
    }
}
void static pack_make_std_response(const DnsPacket*query,DnsPacket**ans,const Vector* ansRR,uint16_t AA) {
    make_response_empty(query,ans);
    DnsPacket * pac = *ans;

    if (AA) AA_SET(pac->header.flags);
    pac->header.answer_RRs = vector_size(ansRR);
    pac->answers = rrs_clone(ansRR);

}
/**
 * 生成服务器内部失败响应包
 */
void pack_make_inner_error(const DnsPacket *query, DnsPacket **answer) {
    make_response_fail(query,answer,RCODE_SERVFAIL);
}

PacketDirection pack_make_response_local(const DnsPacket *query, DnsPacket **response) {
    switch (OPCODE_GET(query->header.flags)) {
        case QUERY:
            //检查问题个数
            if (query->header.qcount<0||query->header.qcount>1) {
                make_response_fail(query,response,RCODE_NOTIMP);
                break;
            }
            if (query->header.qcount == 0) {
                make_response_empty(query,response);
                break;
            }
            // 前置业务检查
            Rcode code;
            query_pre_validate(query,&code);
            if (code!=RCODE_NOERROR) {
                make_response_fail(query,response,code);
                break;
            }
            // 现在才真正开始回答
            //查看缓存
            Vector *ansRR = vector_create(5);
            SectionQuestion *q = vector_get(query->questions, 0);
            if (dns_cache_get(q->qname, q->qtype, q->qclass, ansRR)) {
                // 缓存没有，看Rd
                vector_free(ansRR);
                if (RD_GET(query->header.flags))
                    return UPSTREAM;
                // 不需要转发
                make_response_empty(query,response);
                break;
            }
            // 查到缓存，构造响应包
            pack_make_std_response(query,response,ansRR,0);

            // 后置业务检查
            query_post_validate(*response,&code);
            if (code!=RCODE_NOERROR) { // 检查不通过，返回对应失败响应
                pack_free(*response);
                make_response_fail(query,response,code);
            }
            break;

        case IQUERY:
            make_response_fail(query,response,RCODE_NOTIMP);
            break;
        case STATUS:
            make_response_status(query,response);
            break;
        default: make_response_empty(query,response);
    }
    return CLIENT;
}


