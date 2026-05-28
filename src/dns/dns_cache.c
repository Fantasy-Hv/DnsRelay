//
// Created by yian on 2026/5/9.
//
// 需要线程安全的实现，使用 <threads.h>
#include "dns/cache.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "infra/utils.h"
/**
 *todo 还剩下一个功能——预先的ip-域名映射表
 * 可以使用配置文件
 * 例如
 * [cache]
 * # this is banned domain list
 * 0.0.0.0 = www.bilibili.com,www.github.com
 * # this is consistent cache
 * 20.3.5.3 = www.baidu.com,(c)a.shangfor.com
 * 像后面那个，如果一个ip对应多个域名，如果知道cname可以用一些自己约定的标记区分。当然完全不管cname也可以。
根据配置文件读出来的这些键和值构造RR并设置永不过期,放到缓存中即可
 */
/**
 * 单条缓存项。
 * 一个缓存项对应一个查询三元组：
 * (qname, qtype, qclass) -> 多条 ResourceRecord
 */
typedef struct {
    // 供 HashMap 使用的实际键，格式为 "qclass|qtype|qname"
    char *key;
    // 保留原始查询信息，便于调试和后续校验
    char *qname;
    uint16_t qtype;
    uint16_t qclass;
    // 一次查询可能对应多条 RR，因此这里用 Vector 存一组副本
    Vector *records;   // T = ResourceRecord*
    // 绝对过期时间戳（毫秒）
    ms expire_at;      // 绝对过期时间戳
} CacheEntry;

/**
 * 整个缓存模块的运行时状态。
 * table 负责按 key 快速定位，entries 负责遍历清理。
 */
typedef struct {
    HashMap *table;    // key -> CacheEntry*
    LinkedList *entries; // T = CacheEntry*
    mtx_t lock;
    int size;
    int capacity;
} DnsCache;

static DnsCache *g_cache;
#define DNS_CACHE_DEFAULT_CAPACITY 1024
#define DNS_CACHE_PRELOAD_FILE "./dnsrelay.txt"
#define DNS_CACHE_PRELOAD_TTL UINT32_MAX

// 链表删除时只需要判断“是不是同一个对象指针”。
static int ptr_compare(T a, T b) {
    return a == b ? 0 : 1;
}

// 某些 key 由 CacheEntry 自己持有并释放，因此从 HashMap 删除时不额外 free key。
static void noop_key_destructor(K key) {
    (void) key;
}

/**
 * 根据查询三元组构造哈希表键。
 * 第一版直接用字符串键，便于复用现有的 hash_func_str / compare_cstr。
 */
static char *cache_key_create(const char *qname, uint16_t qtype, uint16_t qclass) {
    if (qname == NULL) {
        return NULL;
    }

    const int key_len = snprintf(NULL, 0, "%u|%u|%s", qclass, qtype, qname);
    if (key_len < 0) {
        return NULL;
    }

    char *key = malloc((size_t) key_len + 1);
    if (key == NULL) {
        return NULL;
    }
    snprintf(key, (size_t) key_len + 1, "%u|%u|%s", qclass, qtype, qname);
    return key;

}

/**
 * 深拷贝一条 RR。
 * 缓存层不直接持有调用方的 RR 指针，而是保存自己的副本。
 */
static ResourceRecord *rr_clone(const ResourceRecord *record) {
    if (record == NULL) {
        return NULL;
    }

    ResourceRecord *copy_rr = rr_create();
    if (copy_rr == NULL) {
        return NULL;
    }

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
 * 释放一条缓存项及其持有的全部 RR 副本。
 */
static void cache_entry_free(CacheEntry *entry) {
    if (entry == NULL) {
        return;
    }

    free(entry->key);
    free(entry->qname);

    if (entry->records != NULL) {
        for (int i = 0; i < vector_size(entry->records); i++) {
            rr_free(vector_get(entry->records, i));
        }
        vector_free(entry->records);
    }
    free(entry);
}

static void cache_entry_clear_records(CacheEntry *entry) {
    if (entry == NULL || entry->records == NULL) {
        return;
    }

    for (int i = 0; i < vector_size(entry->records); i++) {
        rr_free(vector_get(entry->records, i));
    }
    vector_free(entry->records);
    entry->records = NULL;
}

static CacheEntry *cache_entry_create(const char *qname, uint16_t qtype, uint16_t qclass, ms expire_at) {
    if (qname == NULL) {
        return NULL;
    }

    CacheEntry *entry = malloc(sizeof(CacheEntry));
    if (entry == NULL) {
        return NULL;
    }
    memset(entry, 0, sizeof(CacheEntry));

    // 注意：这里存的是“问题键”和 RR 副本，不直接持有调用方传进来的 record。
    entry->key = cache_key_create(qname, qtype, qclass);
    entry->qname = strdup(qname);
    entry->qtype = qtype;
    entry->qclass = qclass;
    entry->records = vector_create(4);
    entry->expire_at = expire_at;

    if (entry->key == NULL || entry->qname == NULL || entry->records == NULL) {
        cache_entry_free(entry);
        return NULL;
    }
    return entry;
}

static int cache_entry_append_rr(CacheEntry *entry, const ResourceRecord *record) {
    ResourceRecord *copy_rr = rr_clone(record);
    if (copy_rr == NULL) {
        return 1;
    }
    vector_add(entry->records, copy_rr);

    const ms expire_at = sys_time_ms() + (ms) record->ttl * 1000;
    if (entry->expire_at == 0 || expire_at < entry->expire_at) {
        entry->expire_at = expire_at;
    }
    return 0;
}

static void cache_remove_entry(CacheEntry *entry) {
    if (g_cache == NULL || entry == NULL) {
        return;
    }

    // 一条缓存项同时被 HashMap 和 LinkedList 引用，删除时两边都要摘掉。
    hash_map_remove(g_cache->table, entry->key, noop_key_destructor);
    linked_list_remove(g_cache->entries, entry, ptr_compare);
    cache_entry_free(entry);
    if (g_cache->size > 0) {
        g_cache->size--;
    }
}

static int dns_cache_prune_locked(void) {
    if (g_cache == NULL) {
        return -1;
    }

    // 这个版本假定调用者已经持有 lock，避免在 put/prune 内部重复加锁。
    const ms now = sys_time_ms();
    LinkedNode *node = g_cache->entries->head;
    while (node != NULL) {
        // 删除当前节点前先保存 next，否则当前节点释放后链表指针会失效。
        LinkedNode *next = node->next;
        CacheEntry *entry = node->data;
        if (entry != NULL && entry->expire_at <= now) {
            cache_remove_entry(entry);
        }
        node = next;
    }
    return 0;
}

static char *trim_space(char *text) {
    if (text == NULL) {
        return NULL;
    }

    while (isspace((unsigned char)*text)) {
        text++;
    }

    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return text;
}

static void strip_inline_comment(char *line) {
    if (line == NULL) {
        return;
    }

    char *comment = strchr(line, '#');
    if (comment != NULL) {
        *comment = '\0';
    }
}

static Qtype ip_text_to_qtype(const char *ip_text) {
    struct in_addr addr4;
    if (inet_pton(AF_INET, ip_text, &addr4) == 1) {
        return QTYPE_A;
    }

    struct in6_addr addr6;
    if (inet_pton(AF_INET6, ip_text, &addr6) == 1) {
        return QTYPE_AAAA;
    }
    return 0;
}

static int preload_rr(const char *name, const char *data, Qtype type) {
    ResourceRecord *rr = rr_make_from_config_pair(name, DNS_CACHE_PRELOAD_TTL, type, data);
    if (rr == NULL) {
        return 1;
    }

    int ret = dns_cache_put(rr);
    rr_free(rr);
    return ret;
}

static int preload_alias_answer_set(const char *alias_name, const char *canonical_name, const char *ip_text, Qtype type) {
    ResourceRecord *alias_rr = rr_make_from_config_pair(alias_name, DNS_CACHE_PRELOAD_TTL, QTYPE_CNAME, canonical_name);
    if (alias_rr == NULL) {
        return 1;
    }

    ResourceRecord *target_rr = rr_make_from_config_pair(canonical_name, DNS_CACHE_PRELOAD_TTL, type, ip_text);
    if (target_rr == NULL) {
        rr_free(alias_rr);
        return 1;
    }

    SectionQuestion question = {
        .qname = alias_rr->name,
        .qtype = type,
        .qclass = QCLASS_IN,
    };
    Vector *answers = vector_create(2);
    if (answers == NULL) {
        rr_free(alias_rr);
        rr_free(target_rr);
        return 1;
    }

    vector_add(answers, alias_rr);
    vector_add(answers, target_rr);
    int ret = dns_cache_put_answer_set(&question, answers);

    rr_free(alias_rr);
    rr_free(target_rr);
    vector_free(answers);
    return ret;
}

static int dns_cache_preload_line(const char *ip_text, char *domains_text) {
    Qtype type = ip_text_to_qtype(ip_text);
    if (type == 0) {
        return 1;
    }

    char *saveptr = NULL;
    char *token = strtok_r(domains_text, ",", &saveptr);
    char *canonical_name = NULL;
    int load_err = 0;
    while (token != NULL) {
        char *item = trim_space(token);
        if (*item != '\0') {
            if (!strncmp(item, "(c)", 3)) {
                char *alias_name = trim_space(item + 3);
                if (*alias_name != '\0' && canonical_name != NULL) {
                    if (preload_alias_answer_set(alias_name, canonical_name, ip_text, type)) {
                        load_err = 1;
                    }
                }
            } else {
                if (canonical_name == NULL) {
                    canonical_name = item;
                }
                if (preload_rr(item, ip_text, type)) {
                    load_err = 1;
                }
            }
        }
        token = strtok_r(NULL, ",", &saveptr);
    }
    return load_err;
}

static int dns_cache_preload_from_file(const char *filepath) {
    FILE *fd = fopen(filepath, "r");
    if (fd == NULL) {
        return 1;
    }

    char line[512];
    int in_cache_section = 0;
    int has_error = 0;
    while (fgets(line, sizeof(line), fd)) {
        strip_inline_comment(line);
        char *content = trim_space(line);
        if (*content == '\0') {
            continue;
        }

        if (*content == '[') {
            char *right = strchr(content, ']');
            if (right == NULL) {
                continue;
            }
            *right = '\0';
            char *section = trim_space(content + 1);
            in_cache_section = !strcmp(section, "cache");
            continue;
        }

        if (!in_cache_section) {
            continue;
        }

        char *equal = strchr(content, '=');
        if (equal == NULL) {
            continue;
        }

        *equal = '\0';
        char *ip_text = trim_space(content);
        char *domains_text = trim_space(equal + 1);
        if (*ip_text == '\0' || *domains_text == '\0') {
            continue;
        }

        if (dns_cache_preload_line(ip_text, domains_text)) {
            has_error = 1;
        }
    }

    fclose(fd);
    return has_error;
}

/**
 * 缓存初始化,main会调用
 * @return
 */
int dns_cache_init() {
    if (g_cache != NULL) {
        return 0;
    }

    DnsCache *cache = malloc(sizeof(DnsCache));
    if (cache == NULL) {
        return 1;
    }

    cache->table = hash_map_create(hash_func_str, compare_cstr);
    if (cache->table == NULL) {
        free(cache);
        return 1;
    }

    cache->entries = linked_list_create();
    if (cache->entries == NULL) {
        hash_map_free(cache->table);
        free(cache);
        return 1;
    }

    if (mtx_init(&cache->lock, mtx_plain) != thrd_success) {
        linked_list_free(cache->entries);
        hash_map_free(cache->table);
        free(cache);
        return 1;
    }

    cache->size = 0;
    cache->capacity = DNS_CACHE_DEFAULT_CAPACITY;
    // 全局单例入口，后续 put/get/prune/free 都通过它访问同一份缓存状态。
    g_cache = cache;
    dns_cache_preload_from_file(DNS_CACHE_PRELOAD_FILE);
    return 0;
}


/**
 * 缓存RR记录
 * @param record
 * @return 0-缓存成功 1-缓存失败
 */
int dns_cache_put(const ResourceRecord * record) {
    if (g_cache == NULL || record == NULL || record->name == NULL || record->ttl == 0) {
        return 1;
    }

    char *key = cache_key_create(record->name, record->type, record->class);
    if (key == NULL) {
        return 1;
    }

    if (mtx_lock(&g_cache->lock) != thrd_success) {
        free(key);
        return 1;
    }

    // 每次写入前顺手清一次过期项，避免 size 被无效数据占满。
    dns_cache_prune_locked();
    if (g_cache->size >= g_cache->capacity) {
        mtx_unlock(&g_cache->lock);
        free(key);
        return 1;
    }

    CacheEntry *entry = NULL;
    if (hash_map_get(g_cache->table, key, (T *) &entry) != 0) {
        // 首次出现这个查询键：新建一条缓存项，并挂到 table + entries 两个容器里。
        entry = cache_entry_create(record->name, record->type, record->class, 0);
        if (entry == NULL) {
            mtx_unlock(&g_cache->lock);
            free(key);
            return 1;
        }
        if (hash_map_put(g_cache->table, entry->key, entry) != 0) {
            cache_entry_free(entry);
            mtx_unlock(&g_cache->lock);
            free(key);
            return 1;
        }
        linked_list_addLast(g_cache->entries, entry);
        g_cache->size++;
    }

    // 无论是新条目还是已有条目，都把 RR 副本追加进去。
    if (cache_entry_append_rr(entry, record)) {
        // 如果这是个刚创建但还没有任何 RR 的空条目，失败时要回滚删掉。
        if (entry->records != NULL && vector_size(entry->records) == 0) {
            cache_remove_entry(entry);
        }
        mtx_unlock(&g_cache->lock);
        free(key);
        return 1;
    }

    mtx_unlock(&g_cache->lock);
    free(key);
    return 0;
}

int dns_cache_put_answer_set(const SectionQuestion *question, Vector *records) {
    if (g_cache == NULL || question == NULL || question->qname == NULL || records == NULL || vector_size(records) == 0) {
        return 1;
    }

    char *key = cache_key_create(question->qname, question->qtype, question->qclass);
    if (key == NULL) {
        return 1;
    }

    if (mtx_lock(&g_cache->lock) != thrd_success) {
        free(key);
        return 1;
    }

    dns_cache_prune_locked();

    CacheEntry *entry = NULL;
    if (hash_map_get(g_cache->table, key, (T *) &entry) != 0) {
        if (g_cache->size >= g_cache->capacity) {
            mtx_unlock(&g_cache->lock);
            free(key);
            return 1;
        }

        entry = cache_entry_create(question->qname, question->qtype, question->qclass, 0);
        if (entry == NULL) {
            mtx_unlock(&g_cache->lock);
            free(key);
            return 1;
        }
        if (hash_map_put(g_cache->table, entry->key, entry) != 0) {
            cache_entry_free(entry);
            mtx_unlock(&g_cache->lock);
            free(key);
            return 1;
        }
        linked_list_addLast(g_cache->entries, entry);
        g_cache->size++;
    } else {
        cache_entry_clear_records(entry);
        entry->records = vector_create(vector_size(records));
        entry->expire_at = 0;
        if (entry->records == NULL) {
            cache_remove_entry(entry);
            mtx_unlock(&g_cache->lock);
            free(key);
            return 1;
        }
    }

    for (int i = 0; i < vector_size(records); i++) {
        ResourceRecord *rr = vector_get(records, i);
        if (rr == NULL || rr->ttl == 0) {
            continue;
        }
        if (cache_entry_append_rr(entry, rr)) {
            cache_remove_entry(entry);
            mtx_unlock(&g_cache->lock);
            free(key);
            return 1;
        }
    }

    if (entry->records == NULL || vector_size(entry->records) == 0) {
        cache_remove_entry(entry);
        mtx_unlock(&g_cache->lock);
        free(key);
        return 1;
    }

    mtx_unlock(&g_cache->lock);
    free(key);
    return 0;
}

/**
 * 根据name和type查询对应的RR
 * @param name RR的name
 * @param type RR类型
 * @param result 结果列表，类型为T=ResourceRecord*,指向缓存中RR的拷贝，传入的列表必须有效,
 * @return 0-命中 ，1-miss
 */
int dns_cache_get(const char* qname,Qtype type,Class qclass,Vector* result) {
    if (g_cache == NULL || qname == NULL || result == NULL) {
        return 1;
    }

    char *key = cache_key_create(qname, type, qclass);
    if (key == NULL) {
        return 1;
    }

    if (mtx_lock(&g_cache->lock) != thrd_success) {
        free(key);
        return 1;
    }

    CacheEntry *entry = NULL;
    if (hash_map_get(g_cache->table, key, (T *) &entry) != 0) {
        mtx_unlock(&g_cache->lock);
        free(key);
        return 1;
    }

    const ms now = sys_time_ms();
    if (entry->expire_at <= now) {
        // 查到了但已经过期：顺手删掉，然后按 miss 返回。
        cache_remove_entry(entry);
        mtx_unlock(&g_cache->lock);
        free(key);
        return 1;
    }

    // 对客户端返回“剩余 TTL”，而不是缓存写入时的原始 TTL。
    const ms remain_ms = entry->expire_at - now;
    const uint32_t remain_ttl = (uint32_t) ((remain_ms + 999) / 1000);
    for (int i = 0; i < vector_size(entry->records); i++) {
        ResourceRecord *copy_rr = rr_clone(vector_get(entry->records, i));
        if (copy_rr == NULL) {
            mtx_unlock(&g_cache->lock);
            free(key);
            return 1;
        }
        copy_rr->ttl = remain_ttl;
        vector_add(result, copy_rr);
    }

    mtx_unlock(&g_cache->lock);
    free(key);
    return 0;
}


/**
 *清理过期缓存,
 * @return 0 正常，-1失败
 */
int dns_cache_prune() {
    if (g_cache == NULL) {
        return -1;
    }

    if (mtx_lock(&g_cache->lock) != thrd_success) {
        return -1;
    }

    // 对外暴露的 prune 负责加锁，真正的遍历删除逻辑在 locked 版本里。
    dns_cache_prune_locked();
    mtx_unlock(&g_cache->lock);
    return 0;
}

int dns_cache_free() {
    if (g_cache == NULL) {
        return 0;
    }

    if (mtx_lock(&g_cache->lock) != thrd_success) {
        return 1;
    }

    // 这里不再走 cache_remove_entry，因为整个模块都要销毁，直接释放对象更简单。
    LinkedNode *node = g_cache->entries->head;
    while (node != NULL) {
        CacheEntry *entry = node->data;
        cache_entry_free(entry);
        node = node->next;
    }

    mtx_unlock(&g_cache->lock);
    mtx_destroy(&g_cache->lock);
    linked_list_free(g_cache->entries);
    hash_map_free(g_cache->table);
    free(g_cache);
    g_cache = NULL;
    return 0;
}
