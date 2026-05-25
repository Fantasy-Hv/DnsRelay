//
// Created by yian on 2026/5/8.
//
//无状态的dns协议解析层
#ifndef DNSRELAY_PROTOCOL_H
#define DNSRELAY_PROTOCOL_H
#include "infra/stl.h"
#include <stdint.h>
#define MAX_PACKET_SIZE 512
//0=查询报文，1=响应报文
#define IS_QUERY(flags) ((flags >> 15)^1)
#define QR_SET(flags) (flags|=0x8000)

typedef enum {
    QUERY, // 标准查询请求
    IQUERY, // 反向查询请求，RFC3425宣布弃用，实际中反向查询使用标准查询+特殊域名的方式表达
    STATUS, // 服务器状态查询请求
}OpCode;
#define OPCODE_GET(flags) ((flags >> 12)&0xf)
//AA标志：响应中的 Answer 段来自该域名的权威名称服务器
#define AA_GET(flags) ((flags >> 10)&1)
#define AA_SET(flags) (flags|= 0x0400)
#define TC_GET(flags) ((flags >> 9)&1)
#define TC_SET(flags) (flags|=0x0200)
#define RD_GET(flags) ((flags >> 8)&1)
#define RD_SET(flags) (flags|=0x0100)
#define RA_GET(flags) ((flags >> 7)&1)
#define RA_SET(flags) (flags |=0x0080)
#define Z_GET(flags) ((flags >> 4)&0x7)
#define Z_SET(flags) (flags|=0x0070)
/**
 * 响应状态码字段值，不要改字段顺序！
 */
typedef enum {
    RCODE_NOERROR, //成功
    RCODE_FORMERR,//格式错误
    RCODE_SERVFAIL,//服务器失败
    RCODE_NXDOMAIN,//域名不存在
    RCODE_NOTIMP,//查询类型不支持
    RCODE_REFUSED//拒绝响应
}Rcode;
#define RCODE(flags) ((flags)&0xf)

typedef enum {
    UPSTREAM,CLIENT
}PacketDirection;

// 所有结构体中的name字段都是标准域名编码，
/**
 * DNS协议的header节
 *
 */
typedef struct  {
    uint16_t id;
    uint16_t flags;
    uint16_t qcount; //问题段中的条目数量，RFC9619规定只能为0或1,其他一律返回NOTIMP/FORMERR :)
    uint16_t answer_RRs; // 回答段的条目数量
    uint16_t authority_RRs; //权威段的条目数量
    uint16_t additional_RRs; // 附加段的条目数量
} SectionHeader;

/**
 * question 节
 */
typedef struct {
    /**
     * 如果以in-addr.arpa 结尾，表明这是一个反向查询
     */
    char* qname; //域名，dns域名编码，不定长字节自解码字段
    uint16_t qtype; // 查询类型
    uint16_t qclass;
}SectionQuestion;
//下面两类值在question和RR中都有使用
typedef enum { // 不要随意更改顺序
    QTYPE_A = 1, // IPv4 地址
    QTYPE_NS, //权威名称服务器，用于auth段中
    QTYPE_MD,
    QTYPE_MF,
    QTYPE_CNAME , //域名的规范名称 用于answer段
    QTYPE_SOA,//授权区域起始
    QTYPE_MB,
    QTYPE_MG,
    QTYPE_MR,
    QTYPE_NULL,//空资源记录 (实验性)
    QTYPE_WKS,//知名服务描述
    QTYPE_PTR,//域名指针 (用于反向解析)
    QTYPE_HINFO,//主机信息 (CPU和操作系统) 用于opcode=status的answer段
    QTYPE_MINFO,//邮箱或邮件列表信息
    QTYPE_MX,
    QTYPE_TXT,//文本字符串
    QTYPE_AAAA = 28, // ipv6
}Qtype;
typedef enum {
    QCLASS_IN=1 // 互联网地址类，只需要支持这个类型即可
}Class;
/**
 * Rdata格式
 *  class   type   rdata
 *  IN      A       大端序4字节ipv4地址
 *  IN      AA      大端序16字节ipv6地址
 *  IN    CNAME     name的标准名称，dns域名编码
 *  IN    NS        该区域的权威服务器域名，dns域名编码
 *  IN    PTR       用于反向dns解析查询 ，此时name为ip地址，有特殊编码规则，对于PTR记录的query,同样只需要
 *  IN    SOA       较为复杂。。。作为中转服务器貌似没有必要解析
 */
typedef struct {
    char* name; //该记录对应question中的哪个域名，dns域名编码，是字符串(\0)
    uint16_t type; //可用值为Qtype的子集
    uint16_t class; // 为IN
    uint32_t ttl; // 缓存过期时间，s
    uint16_t rdata_length; //rdata长度，以字节为单位
    char* rdata;
} ResourceRecord;


typedef struct {
    SectionHeader header; // 控制信息+后续段数量
    Vector*  questions; // T = SessionQuestion*
    //下面三个段的列表元素类型T = ResourceRecord*，并且是可选的
    /**
     * 对question的直接回答
     */
    Vector*  answers;
    /**
     * 这个段用来提供权威服务器的信息
      type  rdata
      NS 当服务器不是查询域的权威，但知道权威服务器是时（即“引用应答”或“委派”），会在 Authority 段返回对应的 NS 记录
     */
    Vector* authorites;
    /**
     * 此段提供辅助数据
     * type     rdata
       A/AAAA   auth段中NS记录的权威服务器的ip
       OPT     用于扩展 DNS 协议，客户端和服务器用它来协商更大的 UDP 包尺寸
     */
    Vector* additionals; // 权威域名服务器的ip
} DnsPacket;
/*
 * RR的类型有很多，每种类型的rdata格式、含义也不一样，
 * 但幸运的是我们不需要关心rdata的内容，因为
 * 考虑RR的生命周期：
 * 当本地缓存为空时，客户端发来请求报文，如果是非主机状态查询，
 * 且客户端希望递归，那么只需要替换id直接转发给上游拿RR即可，不需要我们组装RR
 * 上游响应回来后，以RR为单位放入缓存，下次可以直接根据Question查到对应RR
 * 全程都不需要解析RR里面的rdata
*/

//初始化一条RR记录
ResourceRecord *rr_create();

/**
 * 根据ip映射配置项生成RR记录，以0.0.0.0=www.bilibili.com为例
 * type=A|AAAA 。
 * name = "www.bilibili.com"
 * data -> "0.0.0.0"
 * 该函数根据这些值构造RR记录返回
 * @param name rr的域名字段，原始字符串，如"www.baidu.com"
 * @param ttl rr的过期时间
 * @param type
 * @param data rr的数据，但是原始配置值
 * @return
 */
ResourceRecord* rr_make_from_config_pair(const char * name,uint32_t ttl,Qtype type,const char* data);
void rr_free(ResourceRecord *rr);
/**
 *
 * @param dns_pack 释放该dns包占用的内存
 */
void pack_free(DnsPacket* dns_pack);

/**
 * 创建一个空dns包，主要是为了初始化RR列表
 * @return
 */
DnsPacket* pack_create();

/**
 * 复制dns包
 * @return
 */
DnsPacket* packet_clone(const DnsPacket* source);
/**
 * 将数据包转为字符串方便日志格式化输出，结尾带\0
 * @param dns_pack dns包
 * @return 字符串地址
 */
char* packet_to_log_string(const DnsPacket* dns_pack);

/**
 * 包是否为请求包
 * @param packet
 * @return 是-1,否-0
 */
int packet_is_query(const DnsPacket* packet);

/**
 * 将下游的查询包转为发给上游的查询包
 * @param query_pack 客户端请求
 * @param relay_id 转发包使用的id
 * @param relay_pack 生成的转发包
 * @return
 */
int pack_make_query_relay(const DnsPacket * query_pack,uint16_t relay_id,DnsPacket** relay_pack) ;

/**
 * 根据上游应答构造响应包
 * @param recv 上游应答
 * @param send 返回给客户端的响应
 * @param client_id 客户端查询包的id
 */
void pack_make_response_relay(const DnsPacket* recv,DnsPacket** send,uint16_t client_id);

/**
 *对到来的请求包尝试构建本地应答
 *这里要对内容进行解析判断
 *@param query 客户端请求包
*@param response 下一步要发送的dns包,如果本机可回答，返回响应包;
 *如果需要查询上游，返回null
 *@return CLIENT-构造成功，可以返回响应，UPSTREAM-构造失败，需要请求上游，response 指向NULL
 */
PacketDirection pack_make_response_local(const DnsPacket* query,DnsPacket** response);

/**
 * 生成服务器内部错误响应包
 *
 */
void pack_make_inner_error(const DnsPacket* query,DnsPacket ** answer ) ;


/**
 * @brief 将网络字节流反序列化为dns包
 * @param raw_pack 网络字节流
 * @param packet
 * @return 0-解析成功，-1解析失败
 */
int pack_deserialize(const char* raw_pack,int len,DnsPacket** packet) ;


/**
 * @brief 将dns包结构序列化为网络字节序的字节流
 * @param dns_pack 要序列化的dns包
 * @param packet_buf 序列化缓冲区，必须预先分配足够的内存！
 * @return 序列化后的dns包大小，异常返回-1
 */
int pack_serialize(const DnsPacket* dns_pack,char*  packet_buf,int buf_size);

#endif //DNSRELAY_PROTOCOL_H