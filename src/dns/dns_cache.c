// 需要线程安全的实现，使用 <threads.h>
#include "dns/cache.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include "infra/config.h"
#include "infra/exception.h"
#include "infra/logger.h"
#include "infra/utils.h"

/**
 * 单条缓存项。
 * 对应一个查询三元组 (qname, qtype, qclass) -> 多条 ResourceRecord
 * LRU 通过 hotter/colder 内嵌在双向链表中。
 */
typedef struct CacheEntry {
    char *key;                  // "qclass|qtype|qname"，CacheEntry 自有拷贝
    CacheValue value;           // 直接内嵌，存放深拷贝后的 RR 列表
    ms created_at;              // 缓存写入时刻，结合 value.rrs 中每条 RR 的 ttl 判断过期
    struct CacheEntry *hotter;  // LRU：更热（更近被访问）的缓存条目
    struct CacheEntry *colder;  // LRU：更冷（更久未被访问）的缓存条目
} CacheEntry;

/**
 * 整个缓存模块的运行时状态。
 *
 * LRU 不变量：
 * - head->colder 指向最热条目，tail->hotter 指向最冷条目
 * - 空链表：head->colder = tail，tail->hotter = head
 * - head / tail 为哨兵节点（malloc），不存储实际缓存数据
 */
typedef struct {
    HashMap *table;             // key → CacheEntry*，O(1) 存取
    CacheEntry *head;           // LRU 哨兵（不持有数据）
    CacheEntry *tail;           // LRU 哨兵（不持有数据）
    mtx_t lock;
    int size;                   // 当前条目数
    int capacity;               // 默认 1024
} DnsCache;

static DnsCache *g_cache;
#define DNS_CACHE_DEFAULT_CAPACITY 1024

// ======================== 构造 key ========================

/**
 * 根据查询三元组构造哈希表键，格式 "qclass|qtype|qname"。
 * 返回值调用者需 free。
 */
static char *cache_key_create(const char *qname, uint16_t qtype, uint16_t qclass) {

    // 计算格式化的字符串所需要的长度，以便后续动态分配恰好大小的缓冲区
    const int key_len = snprintf(NULL, 0, "%u|%u|%s", qclass, qtype, qname);
    if (key_len < 0)
        return NULL;


    char *key = malloc((size_t) key_len + 1);
    if (key == NULL)
        return NULL;

    snprintf(key, (size_t) key_len + 1, "%u|%u|%s", qclass, qtype, qname);
    return key;
}



// ======================== CacheValue 辅助 ========================

/**
 * 释放 CacheValue 中的所有 RR 及 Vector 容器本身。
 */
static void free_cache_value(CacheValue *value) {
    if (value == NULL || value->rrs == NULL)
        return;

    //释放列表中的RR
    for (int i = 0; i < vector_size(value->rrs); i++)
        rr_free(vector_get(value->rrs, i));

    // 释放列表
    vector_free(value->rrs);
    value->rrs = NULL;
    value->answer_RRs = 0;
    value->authority_RRs = 0;
    value->additional_RRs = 0;
}

/**
 * 深拷贝 CacheValue：将 src 中的所有 RR 独立拷贝到 dst.rrs。
 * dst 必须已分配（可以是栈上对象），其原有内容会被覆盖。
 * @return 0-失败，1-成功
 */
static int cache_value_clone(CacheValue *dst, const CacheValue *src) {
    if (dst == NULL || src == NULL)
        return 0;


    const int src_size = src->rrs ? vector_size(src->rrs) : 0;
    const int init_cap = src_size > 0 ? src_size : 4;

    dst->answer_RRs = src->answer_RRs;
    dst->authority_RRs = src->authority_RRs;
    dst->additional_RRs = src->additional_RRs;
    dst->rrs = vector_create(init_cap);
    if (dst->rrs == NULL)
        return 0;


    for (int i = 0; i < src_size; i++) {
        ResourceRecord *copy = rr_clone(vector_get(src->rrs, i));
        if (copy == NULL) {
            free_cache_value(dst);
            return 0;
        }
        vector_add(dst->rrs, copy);
    }
    return 1;
}

// ======================== CacheEntry 生命周期 ========================

/**
 * 创建一条缓存条目，深拷贝传入的 CacheValue。
 * @return 新条目指针，失败返回 NULL
 */
static CacheEntry *cache_entry_create(const char *qname, uint16_t qtype, uint16_t qclass,
                                      const CacheValue *cache_value) {

    CacheEntry *entry = malloc(sizeof(CacheEntry));
    memset(entry, 0, sizeof(CacheEntry));

    entry->key = cache_key_create(qname, qtype, qclass);
    if (entry->key == NULL) {
        free(entry);
        return NULL;
    }

    if (!cache_value_clone(&entry->value, cache_value)) {
        free(entry->key);
        free(entry);
        return NULL;
    }

    entry->created_at = sys_time_ms();
    return entry;
}

/**
 * 释放 CacheEntry 及其持有的所有内存（key + CacheValue 中的 RR 列表）。
 */
static void cache_entry_free(CacheEntry *entry) {
    if (entry == NULL)
        return;

    free(entry->key);
    free_cache_value(&entry->value);
    free(entry);
}

// ======================== 过期判断 ========================

/**
 * 检查一条缓存条目是否过期。
 * 遍历 entry 中的所有 RR，若任一条 RR 已过期且其 ttl 不是 UINT32_MAX（永不过期），则认为整条过期。
 * @return 1-已过期，0-未过期
 */
static int is_entry_expired(const CacheEntry *entry) {
    if (entry == NULL || entry->value.rrs == NULL)
        return 1; // 没有数据视为过期


    const ms now = sys_time_ms();
    for (int i = 0; i < vector_size(entry->value.rrs); i++) {
        const ResourceRecord *rr = vector_get(entry->value.rrs, i);

        if (rr == NULL||rr->ttl == UINT32_MAX) // 永不过期
            continue;

        if ((now - entry->created_at) / 1000 >= (ms) rr->ttl)
            return 1;

    }
    return 0;
}

// ======================== LRU 操作 ========================

/**
 * 从 LRU 双向链表中摘出 entry（不释放任何内存）。
 * 仅调整 hotter/colder 指针，将 entry 从链表中安全地移除。
 */
static void lru_remove(CacheEntry *entry) {
    if (entry->hotter == NULL || entry->colder == NULL)
        return;

    entry->hotter->colder = entry->colder;
    entry->colder->hotter = entry->hotter;
    entry->hotter = NULL;
    entry->colder = NULL;
}

/**
 * 将 entry 移至 LRU 链表头部（标记为最近访问）。
 * 先摘出再头插到 head 哨兵之后。
 */
static void lru_touch(DnsCache *cache, CacheEntry *entry) {
    if (cache == NULL || entry == NULL)
        return;

    // 从当前位置摘出（如果已在新条目中 hoter/colder 为 NULL，lru_remove 会安全跳过）
    if (entry->hotter != NULL && entry->colder != NULL)
        lru_remove(entry);

    // 头插
    entry->colder = cache->head->colder;
    entry->hotter = cache->head;
    cache->head->colder->hotter = entry;
    cache->head->colder = entry;
}


/**
 * 从 HashMap 和 LRU 链表中完全移除一条缓存条目，并释放其内存。
 * 注意：hash_map_remove 传入 free 以释放 HashMapEntry 持有的 key（先前 strdup 的拷贝）。
 */
static void cache_remove_entry(CacheEntry *entry) {
    if (g_cache == NULL || entry == NULL)
        return;


    lru_remove(entry);

    // hash_map_remove 通过 entry->key 搜索并删除；传入 free 释放 HashMapEntry.key 的 strdup 副本
    hash_map_remove(g_cache->table, entry->key, free);
    cache_entry_free(entry);

    if (g_cache->size > 0)
        g_cache->size--;

}

// ======================== 过期清理 ========================

/**
 * 内部版本：假定调用者已持有 lock。
 * 遍历 LRU 链表，删除所有已过期的条目。
 */
static int dns_cache_prune_locked(void) {
    if (g_cache == NULL)
        return -1;


    CacheEntry *entry = g_cache->head->colder;
    while (entry != g_cache->tail) {
        // 保存 next，因为当前 entry 可能在过期后被释放
        CacheEntry *next = entry->colder;
        if (is_entry_expired(entry))
            cache_remove_entry(entry);

        entry = next;
    }
    return 0;
}



/**
 * 解析 ip 映射表文件，格式：域名=ip，每行一条，支持 # 单行注释。
 * 按 (qname, qtype) 聚合多条相同question的记录，生成 CacheValue 写入缓存。
 */
static int load_ip_table(const char *path) {
    // 打开文件
    FILE *fd = fopen(path, "r");
    if (!fd) {
        do_log(DEBUG, "ip table file not found: %s", path);
        return 1;
    }

    // 临时数据容器
    HashMap *rr_groups = hash_map_create(hash_func_str, compare_cstr);
    Vector *group_keys = vector_create(16);
    if (rr_groups==NULL||group_keys==NULL) {
        ex_throw("load_ip_table:group container create failed");
        return 1;
    }

    // 遍历每行，解析RR并加入RR组
    char line[512];
    int line_no = 0;
    while (fgets(line, sizeof(line), fd)) {

        // trim
        line_no++;
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '\n' || *p == '#') continue;

        char *eq = strchr(p, '=');
        if (!eq) {
            do_log(WARN, "ip table line %d: missing '=', skipping", line_no);
            continue;
        }

        *eq = '\0';
        // 获取域名串[domain,end]
        char *domain = p;
        char *end = eq - 1;
        // trim
        while (end >= domain && isspace((unsigned char)*end)) { *end = '\0'; end--; }

        // 获取ip串
        char *ip_str = eq + 1;
        // trim
        while (isspace((unsigned char)*ip_str)) ip_str++;
        end = ip_str + strlen(ip_str) - 1;
        while (end >= ip_str && (isspace((unsigned char)*end) || *end == '\n' || *end == '\r')) { *end = '\0'; end--; }

        // 空串检查
        if (*domain == '\0' || *ip_str == '\0') {
            do_log(WARN, "ip table line %d: empty domain or ip, skipping", line_no);
            continue;
        }

        // 解析ip串，获取类型
        Qtype type;
        struct in_addr v4;
        struct in6_addr v6;
        if (inet_pton(AF_INET, ip_str, &v4) == 1)
            type = QTYPE_A;
        else if (inet_pton(AF_INET6, ip_str, &v6) == 1)
            type = QTYPE_AAAA;
         else {
            do_log(WARN, "ip table line %d: invalid ip '%s', skipping", line_no, ip_str);
            continue;
        }

        // 构造RR
        ResourceRecord *rr = rr_make_from_config_pair(domain, UINT32_MAX, type, ip_str);
        if (!rr) {
            do_log(WARN, "ip table line %d: rr_make failed for %s=%s, skipping", line_no, domain, ip_str);
            continue;
        }

        // 同域名的RR记录合并为一组
        char group_key[512];
        snprintf(group_key, sizeof(group_key), "%u|%s", (unsigned)type, rr->name);

        // 获取RR组
        Vector *rr_group = NULL;
        if (hash_map_get(rr_groups, group_key, (T*)&rr_group) != 0 || !rr_group) { // 组中第一条记录
            rr_group = vector_create(5);
            char *dup_key = strdup(group_key);
            hash_map_put(rr_groups, dup_key, rr_group);
            vector_add(group_keys, dup_key);
        }

        vector_add(rr_group, rr);
    }
    fclose(fd);

    // 将所有RR组存进缓存
    for (int i = 0; i < vector_size(group_keys); i++) {
        char *group_key = vector_get(group_keys, i);
        Vector *rr_group = NULL;
        if (hash_map_get(rr_groups, group_key, (T*)&rr_group) != 0 || !rr_group) continue;

        //从key中 解析 qname 和 type
        char *separator = strchr(group_key, '|');
        if (!separator) continue;
        Qtype group_type = (Qtype)atoi(group_key); // atoi: 扫描数字字符串，遇到非数字即停止
        char *qname = separator + 1;

        // 组装CacheValue 并放入缓存
        CacheValue cache_value = {0};
        cache_value.rrs = rr_group;
        cache_value.answer_RRs = (uint16_t)vector_size(rr_group);
        dns_cache_put(qname, group_type, QCLASS_IN, cache_value);

        // put内部会对rr做深拷贝，这里需要释放临时的RR列表
        for (int j = 0; j < vector_size(rr_group); j++)
            rr_free(vector_get(rr_group, j));
        vector_free(rr_group);
    }

    // 释放临时容器
    for (int i = 0; i < vector_size(group_keys); i++)
        free(vector_get(group_keys, i));
    vector_free(group_keys);
    hash_map_free(rr_groups);

    return 0;
}

// ======================== 公开接口 ========================

/**
 * 缓存初始化。main 启动时调用一次。
 * @return 0-成功，1-失败
 */
int dns_cache_init() {
    if (g_cache != NULL) {
        do_log(DEBUG,"repeated call of cache init");
        return 0;
    }

    DnsCache *cache = malloc(sizeof(DnsCache));
    if (cache == NULL) {
        ex_throw("cache alloc failed:%s",strerror(errno));
        return 1;
    }

    cache->table = hash_map_create(hash_func_str, compare_cstr);
    if (cache->table == NULL) {
        ex_throw("cache table alloc failed");
        free(cache);
        return 1;
    }

    // 初始化 LRU 哨兵节点
    cache->head = malloc(sizeof(CacheEntry));
    cache->tail = malloc(sizeof(CacheEntry));
    if (cache->head == NULL || cache->tail == NULL) {
        ex_throw("lru pivot alloc failed :%s",strerror(errno));
        free(cache->head);
        free(cache->tail);
        hash_map_free(cache->table);
        free(cache);
        return 1;
    }
    memset(cache->head, 0, sizeof(CacheEntry));
    memset(cache->tail, 0, sizeof(CacheEntry));
    cache->head->colder = cache->tail;
    cache->tail->hotter = cache->head;

    // 创建锁
    if (mtx_init(&cache->lock, mtx_plain) != thrd_success) {
        ex_throw("mtx init failed : %s",strerror(errno));
        free(cache->head);
        free(cache->tail);
        hash_map_free(cache->table);
        free(cache);
        return 1;
    }

    cache->size = 0;
    cache->capacity = DNS_CACHE_DEFAULT_CAPACITY;
    g_cache = cache;

    char *ip_path = VALUE_DEFAULT_IP_TABLE_PATH;
    config_get(SECTION_DNS, KEY_IP_TABLE_PATH, (T*)&ip_path);

    if (load_ip_table(ip_path) == 0)
        do_log(INFO, "ip table loaded: %s", ip_path);
    return 0;
}

/**
 * 缓存RR记录
* * @param qname      查询域名
 * @param type       查询类型
 * @param qclass     查询类
 * @param cache_value 要缓存的 RR 列表（只读，内部深拷贝）
 * @return 0-缓存成功，1-缓存失败

 *
 */
int dns_cache_put(const char *qname, Qtype type, Class qclass, CacheValue cache_value) {
    if (g_cache == NULL || qname == NULL || cache_value.rrs == NULL)
        return 1;


    /*
     * 流程：
     * 1. 构造查询 key
     * 2. 加锁
     * 3. prune 清理过期条目
     * 4. 如果 key 已存在 → 释放旧 value.rrs，深拷贝新 RR，更新 created_at，LRU 移至头部
     * 5. 如果 key 不存在 → 容量满则淘汰最冷条目 → 创建 CacheEntry → hash_map_put + LRU 头插
     * 6. 解锁
     */

    char *key = cache_key_create(qname, type, qclass);

    // 加锁
    if (mtx_lock(&g_cache->lock) != thrd_success) {
        free(key);
        return 1;
    }

    // make entry
    CacheEntry *entry = NULL;
    if (hash_map_get(g_cache->table, key, (T *) &entry) == 0 && entry != NULL) {
        // key 已存在：原地更新
        CacheValue new_value ;
        if (!cache_value_clone(&new_value, &cache_value)) {
            // 深拷贝失败
            ex_throw("cache_put: cache_value_clone failed");
            mtx_unlock(&g_cache->lock);
            free(key);
            return 1;
        }
        // 释放旧的rrs，挂载新的rrs
        free_cache_value(&entry->value);
        entry->value = new_value;
        entry->created_at = sys_time_ms();
        lru_touch(g_cache, entry);
    } else {
        // key 不存在：新建条目

        // 容量检查：满则淘汰最冷条目（tail->hotter）
        if (g_cache->size >= g_cache->capacity) {
            CacheEntry *victim = g_cache->tail->hotter;
            if (victim != g_cache->head) {
                do_log(DEBUG,"cache full, evict: [%s,%u]",qname,type);
                cache_remove_entry(victim);
            }
        }

        entry = cache_entry_create(qname, type, qclass, &cache_value);

        // hash_map_put 直接存储 key 指针，所以需要 strdup 让 HashMapEntry 持有独立拷贝
        char *map_key = strdup(entry->key);
        hash_map_put(g_cache->table, map_key, entry);

        // LRU 头插
        lru_touch(g_cache, entry);
        g_cache->size++;
    }

    mtx_unlock(&g_cache->lock);
    free(key);
    return 0;
}

/**
 * 根据查询三元组查询缓存。
 *
 * 流程：
 * 1. 构造查询 key
 * 2. 加锁
 * 3. HashMap 查找
 * 4. 过期检查 → 过期则删除并返回 miss
 * 5. 深拷贝 CacheValue 到 result（调用方负责释放 result->rrs）
 * 6. LRU 移至头部
 * 7. 解锁
 *
 * @param qname   查询域名
 * @param type    查询类型
 * @param qclass  查询类
 * @param result  输出参数，命中时填充深拷贝的 CacheValue，调用方用完后需 free_rrs(result.rrs)
 * @return 0-命中，1-miss
 */
int dns_cache_get(const char *qname, Qtype type, Class qclass, CacheValue *result) {
    if (g_cache == NULL || qname == NULL || result == NULL)
        return 1;


    // 这个key是临时堆内存数据，需要free
    char *key = cache_key_create(qname, type, qclass);
    if (key == NULL)
        return 1;


   // 加锁
    if (mtx_lock(&g_cache->lock) != thrd_success) {
        free(key);
        return 1;
    }

    // 获取缓存条目
    CacheEntry *entry = NULL;
    if (hash_map_get(g_cache->table, key, (T *) &entry) != 0 || entry == NULL) {
        mtx_unlock(&g_cache->lock);
        free(key);
        return 1;
    }

    // 过期检查
    if (is_entry_expired(entry)) {
        cache_remove_entry(entry);
        do_log(TRACE,"cache hit but expired : [%s,%u]",qname,type);
        mtx_unlock(&g_cache->lock);
        free(key);
        return 1;
    }

    // 深拷贝到 result
    memset(result, 0, sizeof(CacheValue));
    if (!cache_value_clone(result, &entry->value)) {
        mtx_unlock(&g_cache->lock);
        ex_throw("cache_get: cache_value_clone failed");
        free(key);
        return 1;
    }

    // LRU 移至头部
    lru_touch(g_cache, entry);

    mtx_unlock(&g_cache->lock);
    free(key);
    return 0;
}

/**
 * 清理过期缓存（外部加锁版本，供 daemon 线程定时调用）。
 * @return 0-正常，-1-失败
 */
int dns_cache_prune() {
    if (g_cache == NULL) {
        ex_throw("cache_prune:cache null");
        return -1;
    }

    if (mtx_lock(&g_cache->lock) != thrd_success) {
        ex_throw("mtx lock failed : %s",strerror(errno));
        return -1;
    }

    dns_cache_prune_locked();
    mtx_unlock(&g_cache->lock);
    return 0;
}

/**
 * 清空所有缓存并释放模块资源。
 *
 * 实现方式：遍历 LRU 链表逐一释放 CacheEntry，
 * hash_map_remove(..., free) 同时释放 HashMapEntry.key 的 strdup 副本，
 * 最后释放 HashMap 空壳 + 哨兵 + DnsCache 结构体本身。
 *
 * @return 0-成功，1-失败
 */
int dns_cache_free() {
    if (g_cache == NULL)
        return 0;


    if (mtx_lock(&g_cache->lock) != thrd_success)
        return 1;


    // 遍历 LRU 释放所有缓存条目
    CacheEntry *entry = g_cache->head->colder;
    while (entry != g_cache->tail) {
        CacheEntry *next = entry->colder;
        // hash_map_remove 通过 entry->key 搜索并释放 HashMapEntry.key 的 strdup 副本
        hash_map_remove(g_cache->table, entry->key, free);
        cache_entry_free(entry);
        entry = next;
    }

    mtx_unlock(&g_cache->lock);
    mtx_destroy(&g_cache->lock);

    // 此时 HashMap 已是空壳，hash_map_free 只释放内部桶结构
    hash_map_free(g_cache->table);
    free(g_cache->head);
    free(g_cache->tail);
    free(g_cache);
    g_cache = NULL;
    return 0;
}
