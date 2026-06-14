#include <errno.h>
#include <stdio.h>
#include "dns/protocol.h"
#include "dns/cache.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "infra/config.h"
#include "infra/exception.h"
#include "infra/logger.h"
#include "infra/utils.h"
#define DNS_REV_BUF_SIZE 1024
static int use_cache = VALUE_DEFAULT_USE_CACHE;


// void (*ConfigCleaner)(const char* key,T value);
int dns_resolver_config_parser(const char* key,const char* value,T* result){
    if (strcasecmp(key,KEY_USE_CACHE)) {
        *result = (T)atoi(value);
        if (*result!=0&& ((long)*result)!=1) {
            *result = (T)1;
            ex_throw("dns_config_parser:usecache config invalid");
            return 1;
        }
    }
    return 0;
}

int dns_resolver_init() {
    // 注册到配置系统并获取配置
     config_register_parser(SECTION_DNS_RESOLVER,dns_resolver_config_parser);
    return config_get(SECTION_DNS_RESOLVER,KEY_USE_CACHE,(T*)&use_cache) ==-1;
}

int is_using_cache() {
    return use_cache;
}
/**
 * 将人类可读的域名字符串转为dns wire编码
 * "www.baidu.com" -> "\x03www\x05baidu\x03com\x00"
 * @return 调用者需要free，失败返回NULL
 */
char *encode_name(const char *domain) {
    if (!domain || !*domain) return NULL;

    size_t len = strlen(domain);
    char *encoded = malloc(len + 2);
    if (!encoded) return NULL;

    const char *read = domain;
    char *write = encoded;
    const char *label_start = domain;

    while (1) {
        if (*read == '.' || *read == '\0') {
            size_t label_len = (size_t)(read - label_start);
            if (label_len > 63) {
                ex_throw("encode_name: label too long");
                free(encoded);
                return NULL;
            }
            *write++ = (char)label_len;
            memcpy(write, label_start, label_len);
            write += label_len;
            if (*read == '\0') {
                *write++ = '\0';
                break;
            }
            label_start = read + 1;
        }
        read++;
    }
    return encoded;
}

/**
 * 将dns wire编码转为人类可读的域名字符串
 * "\x03www\x05baidu\x03com\x00" -> "www.baidu.com"
 * @return 调用者需要free，失败返回NULL
 */
char *decode_name(const char *encoded) {
    if (!encoded) return NULL;

    const char *p = encoded;
    size_t total = 0;
    while (*p) {
        if ((*p & 0xC0) == 0xC0) return NULL;
        total += (unsigned char)*p + 1;
        p += (unsigned char)*p + 1;
    }
    total += 1;

    char *decoded = malloc(total);
    if (!decoded) return NULL;

    p = encoded;
    char *write = decoded;
    while (*p) {
        int label_len = (unsigned char)*p;
        p++;
        memcpy(write, p, label_len);
        write += label_len;
        p += label_len;
        if (*p) *write++ = '.';
    }
    *write = '\0';
    return decoded;
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
    free(rr->name);
    free(rr->rdata);
    free(rr);
}

ResourceRecord* rr_make_from_config_pair(const char* name, uint32_t ttl, Qtype type, const char* data) {
    if (!name || !data) return NULL;

    ResourceRecord* rr = rr_create();

    rr->name = encode_name(name);
    if (!rr->name) {
        rr_free(rr);
        return NULL;
    }

    rr->type = type;
    rr->class = QCLASS_IN;
    rr->ttl = ttl;

    if (type == QTYPE_A) {
        rr->rdata = malloc(4);
        if (!rr->rdata || inet_pton(AF_INET, data, rr->rdata) != 1) {
            rr_free(rr);
            return NULL;
        }
        rr->rdata_length = 4;
    } else if (type == QTYPE_AAAA) {
        rr->rdata = malloc(16);
        if (!rr->rdata || inet_pton(AF_INET6, data, rr->rdata) != 1) {
            rr_free(rr);
            return NULL;
        }
        rr->rdata_length = 16;
    } else if (type == QTYPE_CNAME) {
        char* encoded = encode_name(data);
        if (!encoded) {
            rr_free(rr);
            return NULL;
        }
        int len = (int)strlen(encoded) + 1;
        rr->rdata = malloc((size_t)len);
        if (!rr->rdata) {
            free(encoded);
            rr_free(rr);
            return NULL;
        }
        memcpy(rr->rdata, encoded, (size_t)len);
        rr->rdata_length = (uint16_t)len;
        free(encoded);
    } else {
        rr_free(rr);
        return NULL;
    }

    return rr;
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
void questions_free(Vector* questions) {
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
    questions_free(dns_pack->questions);
    free_rrs(dns_pack->rrs);
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
    memset(&pack->header,0,sizeof(SectionHeader)); // 标志位初始化为全0
    pack->questions = vector_create(5);
    pack->rrs = vector_create(5);
    return pack;
}

// 深拷贝RR
ResourceRecord *rr_clone(const ResourceRecord *record) {
    if (record == NULL)
        return NULL;

    ResourceRecord *copy_rr = rr_create();

    copy_rr->name = strdup(record->name);
    copy_rr->type = record->type;
    copy_rr->class = record->class;
    copy_rr->ttl = record->ttl;
    copy_rr->rdata_length = record->rdata_length;

    if (copy_rr->name == NULL) {
        rr_free(copy_rr);
        return NULL;
    }

    copy_rr->rdata = malloc(record->rdata_length);
    if (copy_rr->rdata == NULL) {
        rr_free(copy_rr);
        return NULL;
    }
    memcpy(copy_rr->rdata, record->rdata, record->rdata_length);
    return copy_rr;
}

/**
 * 完备地克隆一个RR列表，
 * @param RRS
 * @return
 */
static Vector *rrs_clone(const Vector *RRS) {
    Vector *clony = vector_create(vector_size(RRS));

    for (int i = 0; i < vector_size(RRS); i++) {
        // 准备
        const ResourceRecord *src = vector_get(RRS, i);
        ResourceRecord *item = malloc(sizeof(ResourceRecord));
        // copy
        memcpy(item, src, sizeof(ResourceRecord));
        item->name = strdup(src->name);
        item->rdata = malloc(src->rdata_length);
        memcpy(item->rdata, src->rdata, src->rdata_length);
        // add
        vector_add(clony, item);
    }
    return clony;
}

DnsPacket *packet_clone(const DnsPacket *source) {
    DnsPacket *packet = pack_create();
    packet->header = source->header;
    //复制问题段
    packet->questions = questions_clone(source->questions);
    packet->rrs  = rrs_clone(source->rrs);
    return packet;
}

/**
 *
 * 解析域名字段
 * @param buf 数据流起始字节
 * @param cur_p 域名起始位置
 * @param res 解析后的dns域名（dns编码）
 * @return 该域名字段的长度,并不是解析后的域名长度
 *
 */
int name_deserialize(const char *buf, const char *cur_p, char **res) {
    /*
    dns编码的格式： n n个字符 m m个字符 两个字节指针
    也就是说，指针会出现在完整标签之后，
    以标准请求 ucloud.bupt.deu.cn 的响应为例
     RR1：
            name： 6 ucloud 4 bupt 3 edu 2 cn 0
            rdata: 2 vn ( c0 ptr_offset_1 两字节指针，指向name的 4 bupt 3 edu 2 cn 0) // 完全可以在域名里藏指针
     RR2：
         name: (c0 ptr_offset_2 两字节指针，指向RR1的rdata

   由此看出，域名解析是一个递归的过程。
   一个域名字段中最多只会出现一个ptr
     */

    // 假设有指针，遍历确定指针位置
    int i = 0;
    while (cur_p[i] && (cur_p[i] & 0xC0) != 0xC0)
        i += cur_p[i] + 1;
    if (!cur_p[i]) {
        //表明这个字段不含指针，直接原样复制
        *res = strdup(cur_p);
        return strlen(cur_p) + 1;
    }
    // 到这里，说明cur_p[i]是指针头,获取指针:
    uint16_t offset;
    memcpy(&offset, (uint16_t *) &cur_p[i], 2);
    n2h_2(&offset);
    offset ^= 0xC000; //高两位置0
    char *suffix;
    // 递归解析
    name_deserialize(buf, buf + offset, &suffix);
    // 结果拼接
    *res = malloc(i + strlen(suffix) + 1);
    memcpy(*res, cur_p, i); // 前一段域名
    memcpy(&(*res)[i], suffix, strlen(suffix) + 1); // 后一段域名
    free(suffix);
    // 一个域名字段只会有一个指针,并且指针是域名最后两个字节，所以到此为止了
    return i + 2;
}

/**
 * 在左闭右开区间搜索字符串
 * @param start_i
 * @param end_e
 * @param name
 * @return  0-没找到，1-找到
 */
static int find_previous_name(const char *start_i, const char *end_e, const char *name, uint16_t *offset) {
    const char *j = start_i;
    while (strcmp(j, name) && j < end_e)
        j++;
    // str_j == name || j=end_e
    if (j == end_e) return 0;
    *offset = j - start_i;
    return 1;
}

/**
 * 将dns编码域名序列化，使用压缩指针
 * @param name dns编码域名
 * @return 序列化的域名占用的字节数
 */
int name_serialize(const char *name, char *const buf, char *const cur_p) {
    uint16_t offset;
    //直接从header之后开始搜索
    const char *search_p = buf + sizeof(SectionHeader);
    // 遍历每个后缀，检查是否可以生成指针
    int i = 0; //从第一个 标签头开始
    while (name[i] && !find_previous_name(search_p, cur_p, name, &offset))
        i += name[i] + 1; //移动到下一个 标签头
    if (name[i]) {
        // 搜索到了
        offset = (offset + sizeof(SectionHeader)) | 0xC000; //设置高两位1,表示这是压缩指针
        memcpy(cur_p, name, i);
        memcpy(&cur_p[i], &offset, 2);
        h2n_2((uint16_t *) &cur_p[i]);
        return i + 2;
    }
    //前面没有出现，只能保留原串了。
    strcpy(cur_p, name);
    return strlen(name) + 1;
}

/**
 *
 * @param cur_p question的起始字节
 * @param question 问题段
 * @return offset:该问题段的字节长度, -1表示异常
 */
static int question_deserialize(const char *cur_p, SectionQuestion *question) {
    //name,正常dns编码，不会出现指针
    if (question==NULL) return 0;
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
static int question_serialize(const SectionQuestion *const question, char *cur_p) {
    char *cursor = cur_p;
    // qname
    strcpy(cursor, question->qname);
    cursor += strlen(question->qname) + 1; //strlen不算\0

    // type
    memcpy(cursor, &question->qtype, 2);
    h2n_2((uint16_t *) cursor);
    cursor += 2;

    //uint16 qclass
    memcpy(cursor, &question->qclass, 2);
    h2n_2((uint16_t *) cursor);
    cursor += 2;

    return cursor - cur_p;
}

/**
 *
 * @param questions
 * @param qc
 * @param cur_p
 * @return
 */
static int questions_serialize(const Vector *const questions, int qc, char *const cur_p) {
    char *cursor = cur_p;
    for (int i = 0; i < qc; i++)
        cursor += question_serialize(vector_get(questions, i), cursor);

    return cursor - cur_p;
}

/**
 *
 * @param cur_p 问题段的起始字节
 * @param qc 问题个数
 * @param segment 存储列表
 * @return 问题段长度
 */
static int questions_deserialize(const char *cur_p, int qc, Vector *const segment) {
    const char *cursor = cur_p;
    for (int i = 0; i < qc; i++) {
        SectionQuestion *question = question_create();
        cursor += question_deserialize(cursor, question);
        vector_add(segment, question);
    }
    return cursor - cur_p;
}

/**
 * 解析rdata
 * @param start_p
 * @param cur_p
 * @param rr
 * @return rdata在数据流中的长度
 */
void rdata_deserialize(const char *start_p, const char *cur_p, ResourceRecord *rr) {
    //如果rdata有域名字段，要把指针换掉
    switch (rr->type) {
        case QTYPE_CNAME:
        case QTYPE_NS:
            name_deserialize(start_p, cur_p, &rr->rdata);
            rr->rdata_length = strlen(rr->rdata) + 1; //已经解析好纯域名了
            break;

        case QTYPE_MX: // 两字节无符号preference + dns编码域名
            uint16_t preference;
            memcpy(&preference, cur_p, 2); // 保持大端，因为序列化时不会处理这个数据
            // 解析域名
            char *name;
            name_deserialize(start_p, cur_p + 2, &name);
            int namel = strlen(name) + 1;
            // 设置字段
            rr->rdata = malloc(2 + namel);
            memcpy(&rr->rdata, &preference, 2);
            memcpy(&rr->rdata[2], name, namel);
            rr->rdata_length = 2 + namel;
            free(name);
            break;

        default:
            rr->rdata = malloc(rr->rdata_length);
            memcpy(rr->rdata, cur_p, rr->rdata_length);
    }
}

/**
 * @param start_p dns报文起始字节
 * @param cur_p 当前待解析的rr的起始字节
 * @param rr 解析完成的rr
 * @return offset:该rr的字节长度
 */
int rr_deserialize(const char *start_p, const char *cur_p, ResourceRecord *const rr) {
    // cur_p ： 指向下一个待解析的字节
    const char *cursor = cur_p;
    cursor += name_deserialize(start_p, cursor, &rr->name);

    //type
    memcpy(&rr->type, cursor, 2);
    n2h_2(&rr->type);
    cursor += 2;

    //class
    memcpy(&rr->class, cursor, 2);
    n2h_2(&rr->class);
    cursor += 2;

    // uint32  ttl
    memcpy(&rr->ttl, cursor, 4);
    n2h_4(&rr->ttl);

    cursor += 4;
    //2B rdata_length
    memcpy(&rr->rdata_length, cursor, 2);
    n2h_2(&rr->rdata_length);
    cursor += 2;

    // rdata
    int rlen = rr->rdata_length;
    rdata_deserialize(start_p, cursor, rr);
    cursor += rlen;

    return cursor - cur_p;
}

int rrs_deserialize(const char *start_p, const char *cur_p, int cnt, Vector *const rrs) {
    const char *cursor = cur_p;
    for (int i = 0; i < cnt; i++) {
        ResourceRecord *rr = rr_create();
        cursor += rr_deserialize(start_p, cursor, rr);
        vector_add(rrs, rr);
    }
    return cursor - cur_p;
}

/*
 * 关于压缩指针，规则是
 * 1.除了question的name,其他出现域名的地方都可以用压缩指针，包括：rr的name和rr的rdata
 * name一定是域名，但rdata只在以下type中是域名:CNAME,NS,MX,SOA,PTR（不支持，可以不管），SRV
 * 2.整个域名字段可以只有指针或域名，也可以前面是域名标签，后面是指针，最多只有一个指针且必须在末尾
 */
/**
 *
 * @param start_p 整个缓冲区起始位置
 * @param cur_p 当前记录起始位置
 * @param rr
 * @return rr序列化的长度
 */
static int rr_serialize(char *start_p, char *cur_p, const ResourceRecord *const rr) {
    char *cursor = cur_p;
    //name
    cursor += name_serialize(rr->name, start_p, cursor);

    //type
    memcpy(cursor, &rr->type, 2);
    h2n_2((uint16_t *) cursor);
    cursor += 2;

    //class
    memcpy(cursor, &rr->class, 2);
    h2n_2((uint16_t *) cursor);
    cursor += 2;

    //ttl
    memcpy(cursor, &rr->ttl, 4);
    h2n_4((uint32_t *) cursor);
    cursor += 4;

    //len
    memcpy(cursor, &rr->rdata_length, 2);
    h2n_2((uint16_t *) cursor);
    cursor += 2;

    //rdata
    memcpy(cursor, rr->rdata, rr->rdata_length);
    cursor += rr->rdata_length;
    return cursor - cur_p;
}

/**
 * 在指定位置开始序列化 RR记录段
 * @param start_p 整个包的起始字节
 * @param cur_p 起始位置
 * @param rrs rr列表
 * @param cnt rr数量
 * @return
 */
static int rrs_serialize(Vector *const rrs, int cnt, char *start_p, char *cur_p,int buff_size) {
    char *cursor = cur_p;

    for (int i = 0; i < cnt; i++) {

        cursor += rr_serialize(start_p, cursor, vector_get(rrs, i));

        // 错误处理
        if (cursor-start_p>buff_size) {
            ex_throw("buff over at %d rr",i+1);
            return cursor-cur_p;
        }
    }
    return cursor - cur_p;
}

/**
 * @brief 将dns包结构序列化为网络字节序的字节流
 * @param dns_pack 要序列化的dns包
 * @param packet_buf 序列化缓冲区，必须预先分配足够的内存！
 * @return 序列化后的dns包大小，异常返回-1,
 */
int pack_serialize(const DnsPacket *dns_pack, char *const packet_buf, int buf_size) {
    do_log(TRACE, "pack to seri : %s", packet_to_log_string(dns_pack));
    char *cursor = packet_buf;

    // header
    memcpy(cursor, &dns_pack->header, sizeof(SectionHeader));
    header_endian_2n((SectionHeader *) cursor);
    cursor += sizeof(SectionHeader);
    if (cursor - packet_buf > buf_size)
        return -1;

    //question
    cursor += questions_serialize(dns_pack->questions, dns_pack->header.qcount, cursor);
    if (cursor - packet_buf > buf_size) {
        ex_throw("pac_seri :buf over at question seg");
        return -1;
    }

    // answer 、authority 、 additional
    int rrs_c = dns_pack->header.answer_RRs + dns_pack->header.authority_RRs + dns_pack->header.additional_RRs;
    cursor += rrs_serialize(dns_pack->rrs, rrs_c, packet_buf, cursor, buf_size);

    do_log(TRACE, "pac_seried_size %d", cursor - packet_buf);
    return cursor - packet_buf;
}

/**
 * @brief 将网络字节流反序列化为dns包
 * @param raw_pack 网络字节流
 * @param len
 * @param packet
 * @return 0-解析成功，-1解析失败
 */
int pack_deserialize(const char *raw_pack, int len, DnsPacket **packet) {
    if (len > 512 || len < 12) {
        ex_throw("pack_deseri: raw size incorrect : %d", len);
        return -1;
    }
    do_log(TRACE, "raw pack size %d", len);

    //初始化
    const char *cursor = raw_pack;
    DnsPacket *pac = pack_create();

    // header
    SectionHeader *header_in_buf = (SectionHeader *) raw_pack;
    memcpy(&pac->header, header_in_buf, sizeof(SectionHeader));
    header_endian_2h(&pac->header);
    cursor += sizeof(SectionHeader);

    // questions
    cursor += questions_deserialize(cursor, pac->header.qcount, pac->questions);
    int rr_c = pac->header.additional_RRs + pac->header.answer_RRs + pac->header.authority_RRs;
    cursor += rrs_deserialize(raw_pack, cursor, rr_c, pac->rrs);

    *packet = pac;
    do_log(DEBUG, "deseried pack  :%s", packet_to_log_string(pac));
    return 0;
}

/**
 * 包是否为请求包
 * @param packet
 * @return 是-1,否-0
 */
int packet_is_query(const DnsPacket *packet) {
    return IS_QUERY(packet->header.flags);
}

static int setRcode(DnsPacket *pack, Rcode rcode) {
    if (!pack) return -1;
    // 16 位，设置低4位，
    pack->header.flags &= 0xfff0; // 全部清零
    pack->header.flags |= 0x000f & rcode; //  直接覆盖
    return 0;
}

/**
 * 构造不带RR的响应。
 * @return
 */
static int make_response_empty(const DnsPacket *query, DnsPacket **empty_response) {
    DnsPacket *response = pack_create();
    response->header = query->header;
    //header.flags
    RA_SET(response->header.flags);
    QR_SET(response->header.flags);
    setRcode(response, RCODE_NOERROR);
    // header.cnts
    response->header.answer_RRs = 0;
    response->header.additional_RRs = 0;
    response->header.authority_RRs = 0;
    // question
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
    make_response_empty(query, fail);
    setRcode(*fail, rcode);
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
static int memallz(const char *mem, int len) {
    while (len > 0 && mem[--len] == 0);
    return !len;
}

/**
 * QR=Q
 * Opcode=Status时调用该方法生成响应包
 * @param query
 * @param response
 * @return
 */
static int make_response_status(const DnsPacket *query, DnsPacket **response) {
    make_response_fail(query, response, RCODE_NOTIMP); // 直接不支持。
    return 0;
}

/**
 * 对构建好的响应包进行检查，可以用来封禁ip
 * @param response
 * @param rcode
 * @return
 */
static void query_post_validate(const DnsPacket *response, Rcode *rcode) {
    //检查ans，如果ip有0.0.0.0返回NXDMAIN
    Vector *ans = response->rrs;
    for (int i = 0; i < response->header.answer_RRs; i++) {
        ResourceRecord *rr = vector_get(ans, i);
        if ((rr->type == QTYPE_A || rr->type == QTYPE_AAAA) && memallz(rr->rdata, rr->rdata_length)) {
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
    *send = packet_clone(recv);
    (*send)->header.id = client_id;  // 仅修改id

    // 不启用缓存则直接返回
    if (!use_cache) return;
    // 缓存资源记录
    SectionQuestion * q = vector_get(recv->questions,0);
    CacheValue value;
    value.answer_RRs = (*send)->header.answer_RRs;
    value.authority_RRs = (*send)->header.authority_RRs;
    value.additional_RRs = (*send)->header.additional_RRs;
    value.rrs = recv->rrs;
    dns_cache_put(q->qname,q->qtype,q->qclass,value);
}

void static pack_make_std_response_local(const DnsPacket *query, DnsPacket **ans, const CacheValue cache) {
    make_response_empty(query, ans);
    DnsPacket *pac = *ans;
    pac->header.answer_RRs = cache.answer_RRs;
    pac->header.authority_RRs = cache.authority_RRs;
    pac->header.additional_RRs = cache.additional_RRs;
    pac->rrs = rrs_clone(cache.rrs);
}

/**
 * 生成服务器内部失败响应包
 */
void pack_make_inner_error(const DnsPacket *query, DnsPacket **answer) {
    make_response_fail(query, answer, RCODE_SERVFAIL);
}

PacketDirection pack_try_response_local(const DnsPacket *query, DnsPacket **response) {
    switch (OPCODE_GET(query->header.flags)) {
        case QUERY:
            //检查问题个数
            if (query->header.qcount > 1) {
                do_log(DEBUG,"qdcount >1");
                make_response_fail(query, response, RCODE_NOTIMP);
                break;
            }
            if (query->header.qcount == 0) {
                do_log(DEBUG,"empty query");
                make_response_empty(query, response);
                break;
            }
            // 前置业务检查
            Rcode code;
            query_pre_validate(query, &code);
            if (code != RCODE_NOERROR) {
                make_response_fail(query, response, code);
                break;
            }
            // 现在才真正开始回答
            //查看缓存
            CacheValue cache_value;
            SectionQuestion *q = vector_get(query->questions, 0);
            do_log(INFO, "id %d,qname: [%s]", query->header.id, q->qname);
            if (!use_cache||dns_cache_get(q->qname, q->qtype, q->qclass, &cache_value)) {
                // 缓存没有，看Rd
                if (use_cache)
                    do_log(DEBUG, "cache miss");

                if (RD_GET(query->header.flags))
                    return UPSTREAM;

                // 客户端不要求递归查询,直接返回
                make_response_empty(query, response);
                break;
            }
            // 查到缓存，构造响应包
            do_log(DEBUG, "cache hit");
            pack_make_std_response_local(query, response, cache_value);
            free_rrs(cache_value.rrs);

            // 后置业务检查
            query_post_validate(*response, &code);
            if (code != RCODE_NOERROR) {
                // 检查不通过，返回对应失败响应
                pack_free(*response);
                make_response_fail(query, response, code);
            }
            break;

        case IQUERY:
            do_log(DEBUG, "iquery recv");
            make_response_fail(query, response, RCODE_NOTIMP);
            break;

        case STATUS:
            do_log(DEBUG, "status query recv");
            make_response_status(query, response);
            break;

        default: make_response_empty(query, response);
    }
    return CLIENT;
}

char *packet_to_log_string(const DnsPacket *dns_pack) {
    static char log_buf[4096];
    char *cursor = log_buf;
    int remaining = sizeof(log_buf);

    if (!dns_pack) {
        snprintf(log_buf, remaining, "NULL packet");
        return log_buf;
    }

    // Header 信息
    int written = snprintf(cursor, remaining,
                           "header: ID=%u QR=%s Opcode=%d AA=%d TC=%d RD=%d RA=%d Z=%d RCODE=%d ",
                           dns_pack->header.id,
                           IS_QUERY(dns_pack->header.flags) ? "Query" : "Response",
                           OPCODE_GET(dns_pack->header.flags),
                           AA_GET(dns_pack->header.flags),
                           TC_GET(dns_pack->header.flags),
                           RD_GET(dns_pack->header.flags),
                           RA_GET(dns_pack->header.flags),
                           Z_GET(dns_pack->header.flags),
                           RCODE(dns_pack->header.flags)
    );
    cursor += written;
    remaining -= written;

    // Question 段数量
    written = snprintf(cursor, remaining, "Qdcnt=%u Ans_RRs=%u auth_RRs=%u Addition_RRs =%u \n ",
                       dns_pack->header.qcount,
                       dns_pack->header.answer_RRs,
                       dns_pack->header.authority_RRs,
                       dns_pack->header.additional_RRs
    );
    cursor += written;
    remaining -= written;

    // Questions
    if (dns_pack->questions && vector_size(dns_pack->questions) > 0) {
        written = snprintf(cursor, remaining, "Questions:\n");
        cursor += written;
        remaining -= written;

        for (int i = 0; i < vector_size(dns_pack->questions) && remaining > 0; i++) {
            SectionQuestion *q = vector_get(dns_pack->questions, i);
            const char *qtype_str = "UNKNOWN";
            switch (q->qtype) {
                case QTYPE_A: qtype_str = "A";
                    break;
                case QTYPE_NS: qtype_str = "NS";
                    break;
                case QTYPE_CNAME: qtype_str = "CNAME";
                    break;
                case QTYPE_SOA: qtype_str = "SOA";
                    break;
                case QTYPE_PTR: qtype_str = "PTR";
                    break;
                case QTYPE_MX: qtype_str = "MX";
                    break;
                case QTYPE_TXT: qtype_str = "TXT";
                    break;
                case QTYPE_AAAA: qtype_str = "AAAA";
                    break;
                default: break;
            }

            written = snprintf(cursor, remaining, "[%s type=%s class=%u]\n",
                               q->qname ? q->qname : "(null)",
                               qtype_str, q->qclass
            );
            cursor += written;
            remaining -= written;
        }
    }

    // Answer RRs
    if (dns_pack->rrs && vector_size(dns_pack->rrs) > 0) {
        written = snprintf(cursor, remaining, "Resource Records :\n");
        cursor += written;
        remaining -= written;

        for (int i = 0; i < vector_size(dns_pack->rrs) && remaining > 0; i++) {
            ResourceRecord *rr = vector_get(dns_pack->rrs, i);
            const char *type_str = "UNK";
            switch (rr->type) {
                case QTYPE_A: type_str = "A";
                    break;
                case QTYPE_NS: type_str = "NS";
                    break;
                case QTYPE_CNAME: type_str = "CNAME";
                    break;
                case QTYPE_SOA: type_str = "SOA";
                    break;
                case QTYPE_PTR: type_str = "PTR";
                    break;
                case QTYPE_MX: type_str = "MX";
                    break;
                case QTYPE_TXT: type_str = "TXT";
                    break;
                case QTYPE_AAAA: type_str = "AAAA";
                    break;
                default: break;
            }

            written = snprintf(cursor, remaining, "[%s type=%s ttl=%u rdlen=%u ]\n",
                               rr->name ? rr->name : "(null)",
                               type_str, rr->ttl, rr->rdata_length
            );
            cursor += written;
            remaining -= written;
        }
    }

    // 确保字符串以 \0 结尾
    log_buf[sizeof(log_buf) - 1] = '\0';
    return log_buf;
}

