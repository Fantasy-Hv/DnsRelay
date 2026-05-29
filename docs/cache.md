# DNS 缓存模块设计方案

## 一、需求约束

1. 协议解析器要求 **O(1) 数据存取** → HashMap
2. 过期检查要求 **遍历删除** → 哈希表虽然能遍历但因为内部有空桶不如线性数据结构好
3. 缓存淘汰要求 **有序** → LRU 双向链表

---

## 二、数据结构

### 2.1 CacheEntry

```c
typedef struct CacheEntry {
    char *key;                  // "qclass|qtype|qname"，HashMap 键
    CacheValue value;           // 直接内嵌，get 时以此为蓝本 clone 返回
    ms created_at;              // 缓存写入时刻，结合value.rr里的ttl来判断该缓存条目是否过期，
    
    // 这两个域构成一个双向链表，同时服务于缓存过期和缓存淘汰
    struct CacheEntry *hotter;  // LRU：更热（更近被访问）的缓存条目
    struct CacheEntry *colder;  // LRU：更冷（更久未被访问）的缓存条目
} CacheEntry;
```
a
**语义说明**：

| 字段 | 来源/含义                                                                   |
|------|-------------------------------------------------------------------------|
| `key` | `"qclass\|qtype\|qname"`，HashMap 键，也用于反查。`qname`/`qtype`/`qclass` 不冗余存储 |
| `value` | put 时深拷贝一份，get 时 deep clone 返回。上层负责释放（`free_rrs`）                       |
| `created_at` | 过期判断：`(now - created_at) / 1000 >= rr->ttl`                             |
| `hotter`/`colder` | 内嵌 LRU 双向链表。head→hotter→...→colder→tail                                 |

### 2.2 DnsCache

```c
typedef struct {
    HashMap *table;             // key → CacheEntry*，O(1) 存取
    CacheEntry *head;           // LRU 哨兵（malloc），不持有数据
    CacheEntry *tail;           // LRU 哨兵（malloc），不持有数据
    mtx_t lock;
    int size;                   // 当前条目数
    int capacity;               // 默认 1024
} DnsCache;
```

**LRU 不变量**：
- `head->colder` 指向最热条目，`tail->hotter` 指向最冷条目
- 空链表：`head->colder = tail`，`tail->hotter = head`
- 哨兵为 `malloc` 得到的指针，不存储实际数据，语义干净，无半合法对象问题

### 2.3 HashMap 遍历

`HashMap` 内部 `buckets: Vector<LinkedList>`，遍历天然可行。需新增接口：

```c
// stl.h 新增
typedef void (*HashMapVisitor)(K key, T value, void *ctx);
void hash_map_foreach(HashMap *map, HashMapVisitor visitor, void *ctx);
```

---

## 三、核心操作

### 3.1 put

```
dns_cache_put(qname, qtype, qclass, cache_value)
│
├─ 1. 参数校验 (qname==NULL || value.rrs==NULL → 返回 1)
├─ 2. 构造 key = "qclass|qtype|qname"
├─ 3. mtx_lock
├─ 4. dns_cache_prune_locked()         // 先清理过期腾空间
│
├─ 5. 查找是否已有该 key 的条目
│     ├─ 已存在 → 释放旧 value.rrs，深拷贝新 RR 列表
│     │           更新 created_at，LRU 移至 head
│     │
│     └─ 不存在 → 容量检查
│           ├─ size >= capacity → 淘汰尾节点（tail->hotter）
│           ├─ 创建 CacheEntry：
│           │   - 深拷贝所有 RR
│           │   - created_at = sys_time_ms()
│           │   - 取所有 RR 中最小的 TTL 作为条目有效期
│           │   - hash_map_put + LRU 头插
│           └─ size++
│
├─ 6. mtx_unlock
└─ 7. free(key)，返回 0
```

### 3.2 get

```
dns_cache_get(qname, qtype, qclass, result)
│
├─ 1. 参数校验 → 构造 key
├─ 2. mtx_lock
├─ 3. hash_map_get → 查找 CacheEntry
│     └─ 未找到 → mtx_unlock → 返回 miss(1)
│
├─ 4. 过期检查：
│     遍历 entry->value.rrs 中每条 RR
│       判断if (sys_time_ms() - created_at) / 1000 >= rr->ttl
            即有 RR 过期 → 整条 cache_entry 删除 → 返回 miss(1)
│     (ttl == UINT32_MAX 的 RR 永不触发此条件)
│
├─ 5. 深拷贝所有 RR：
│     填充 result->rrs, result->answer_RRs, etc. 返回给上层
│
├─ 6. LRU 移至 head（摘出后头插）
├─ 7. mtx_unlock → 返回 hit(0)
```

### 3.3 内存所有权（key）

一个缓存条目涉及**三份**独立的 key 字符串拷贝，各有各的所有者：

| 持有者 | 来源 | 何时释放 |
|--------|------|----------|
| `HashMapEntry.key` | `strdup(CacheEntry.key)`，`hash_map_put` 时拷贝 | `hash_map_remove(..., free)` |
| `CacheEntry.key` | `strdup(...)`，`cache_entry_create` 时生成 | `cache_entry_free` |
| prune 临时收集 | `strdup(HashMapEntry.key)`，第一阶段收集时拷贝 | 第二阶段 `free(key)` |

**禁止共享指针**：`hash_map_put(map, entry->key, entry)` 是错误的——会使 HashMapEntry 和 CacheEntry 指向同一块内存，任意一方释放后另一方持有野指针。

### 3.4 Prune（过期清理）

直接便利lru链表，逐个检查过期。

**调用时机**：
- daemon 线程每 4 秒定时调用 `dns_cache_prune`（外部加锁版本）
- put 写入前内部调用 `dns_cache_prune_locked`（内部不加锁版本）

### 3.4 缓存淘汰（LRU）

```
容量满时的淘汰流程：
│
├─ 取 tail->hotter（最冷条目）
├─ lru_remove（仅仅是将cache_entry摘出链表，没有任何free操作） + hash_map_remove + cache_entry_free
├─ size--
└─ 为新条目腾出空间
```

**put/get 中的 LRU 位置更新**：
- **put 新条目**：头插到 head 之后
- **put 已有条目**：1.覆盖原有rr的ttl, 2.摘出后头插（覆盖写入视为新访问）
- **get 命中**：摘出后头插

**摘出 + 头插**（`lru_touch`）：



---

## 四、IP 映射表加载

### 4.1 配置

`dnsrelay.ini` 中通过 `[dns]` 节指定映射文件路径：

```ini
[dns]
iptable=./dnsrelay.txt
```

不需要注册配置解析器——`config_get` 直接返回原始字符串。如果节或键不存在，优雅降级为默认路径 `./dnsrelay.txt`。

### 4.2 文件格式

```
# IP 为 0.0.0.0 表示域名被封禁
0.0.0.0 = www.bilibili.com,www.github.com

# 静态 DNS 映射
20.3.5.3 = www.baidu.com,(c)a.shangfor.com
```

| 语法 | 说明                                              |
|------|-------------------------------------------------|
| `#` 开头 | 注释                                              |
| `IP = domains` | IP 可为 IPv4/IPv6；domains 逗号分隔                    |
| `(c)` 前缀 | 应该为此域名生成 CNAME 类型RR，rdata 指向同行第一个非 (c) 域名       |
| `0.0.0.0` | 封禁：rdata 全零 → `query_post_validate` 返回 NXDOMAIN |

### 4.3 处理流程

```
dns_cache_load_ip_table()
│
├─ config_get("dns", "iptable", &path) → 不存在则用 "./dnsrelay.txt"
├─ fopen(path)
│
├─ 逐行：跳过空行/注释 → 按 '=' 分割
│   ├─ 左：inet_pton 解析 IP → 确定 type(A/AAAA)
│   ├─ 右：按 ',' 分割域名 → 识别 (c) 前缀
│   │
│   └─ 对每个域名：
│       ├─ 普通域名 → 构造 A/AAAA RR (ttl=UINT32_MAX)
│       │              → dns_cache_put(qname, type, IN, value)
│       │
│       └─ (c)域名 → 构造 CNAME RR + 附带目标域名的 A/AAAA RR
│                     → dns_cache_put(qname, A, IN, value)
│
└─ fclose → 日志输出加载统计
```

### 4.4 rr_make_from_config_pair

在 `dns_resolver.c` 中实现，从原始配置值构造一条 RR：

```c
ResourceRecord* rr_make_from_config_pair(const char* name,   // 人类可读域名
                                          uint32_t ttl,      // UINT32_MAX
                                          Qtype type,        // A/AAAA/CNAME
                                          const char* data); // IP 串 或 目标域名
```

---

## 五、线程安全

| 操作 | 锁策略 |
|------|--------|
| `dns_cache_put` | 全程持锁（内部调 `prune_locked`，不重复加锁） |
| `dns_cache_get` | 全程持锁（clone + LRU 移动） |
| `dns_cache_prune` | 加锁 → `prune_locked` → 解锁 |
| `dns_cache_load_ip_table` | 内部每次 put 自动加锁 |
| `dns_cache_free` | 加锁遍历释放 → 解锁销毁 |

---

## 六、改动文件清单

| 文件 | 改动 |
|------|------|
| `include/infra/stl.h` | 新增 `hash_map_foreach` 声明 |
| `src/infra/stl.c` | 实现 `hash_map_foreach` |
| `include/dns/cache.h` | 新增 `dns_cache_load_ip_table` 声明 |
| `src/dns/dns_cache.c` | **重写**：新 CacheEntry/DnsCache，put/get/prune/free，load_ip_table |
| `src/dns/dns_resolver.c` | 新增 `rr_make_from_config_pair` 实现 |
| `src/main.c` | `dns_cache_init` 后调用 `dns_cache_load_ip_table` |
| `test/test_cache.c` | 适配新 API |

---

## 七、可测试性

- **过期**：短 TTL 写入 → sleep → get 返回 miss
- **LRU 淘汰**：填满 cache → put 新条目 → 验证最冷条目被淘汰
- **IP 映射**：加载测试文件 → get 验证返回正确的 A/AAAA/CNAME RR
- **永不过期**：`ttl=UINT32_MAX` 的条目 get 永远 hit
