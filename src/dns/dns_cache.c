//
// Created by yian on 2026/5/9.
//
// 需要线程安全的实现，使用 <threads.h>
#include "dns/cache.h"

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

static CacheEntry *cache_entry_create(const ResourceRecord *record) {
    if (record == NULL || record->name == NULL) {
        return NULL;
    }

    CacheEntry *entry = malloc(sizeof(CacheEntry));
    if (entry == NULL) {
        return NULL;
    }
    memset(entry, 0, sizeof(CacheEntry));

    // 注意：这里存的是“查询键”和 RR 副本，不直接持有调用方传进来的 record。
    entry->key = cache_key_create(record->name, record->type, record->class);
    entry->qname = strdup(record->name);
    entry->qtype = record->type;
    entry->qclass = record->class;
    entry->records = vector_create(4);
    // 先按当前这条 RR 的 TTL 计算一个初始过期时间。
    entry->expire_at = sys_time_ms() + (ms) record->ttl * 1000;

    if (entry->key == NULL || entry->qname == NULL || entry->records == NULL) {
        cache_entry_free(entry);
        return NULL;
    }
    return entry;
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
        entry = cache_entry_create(record);
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
    ResourceRecord *copy_rr = rr_clone(record);
    if (copy_rr == NULL) {
        // 如果这是个刚创建但还没有任何 RR 的空条目，失败时要回滚删掉。
        if (entry->records != NULL && vector_size(entry->records) == 0) {
            cache_remove_entry(entry);
        }
        mtx_unlock(&g_cache->lock);
        free(key);
        return 1;
    }
    vector_add(entry->records, copy_rr);

    const ms expire_at = sys_time_ms() + (ms) record->ttl * 1000;
    // 同一查询键下可能会追加多条 RR。
    // 这里取“更早过期”的时间，保证整组缓存不会比任何单条 RR 活得更久。
    if (entry->expire_at == 0 || expire_at < entry->expire_at) {
        entry->expire_at = expire_at;
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
