# DnsRelay 架构设计文档

> 本文档基于 `src/` 和 `include/` 目录下的实际源码生成，忠实地反映系统的设计思想与实现意图。

---

## 一、设计思想

### 1.1 单例模块化

每个模块在 `.c` 文件中通过 `static` 变量维护自己的运行时状态，对外表现为**单例对象**。头文件只暴露函数声明，不暴露任何变量。头文件中未声明的函数，源文件中必须声明为 `static`。

这种设计等价于面向对象中的"一个类只有一个实例"，但完全通过 C 语言的编译单元边界来实现封装。

### 1.2 分层架构

系统采用严格的分层架构，自上而下为：

```
main.c (入口)
  └─ server/ (服务层)
       └─ dns/ (核心业务层)
            └─ infra/ (基础设施层)
```

**单向依赖**：上层持有下层的数据结构并调用下层 API，下层完全不感知上层的存在。

### 1.3 错误处理

将错误处理的两个维度分离：

- **控制维度**（怎么办）：通过函数返回值（`int`）传递，只管"该不该继续执行"
- **诊断维度**（为什么）：通过隐式的 `thread_local` 异常栈累积错误上下文，最终由顶层一次性输出完整调用链

一个错误从发生到被处理的完整上下文是一条日志，而非多条分散的日志。这在多线程环境下也能保证不同线程的错误信息不会混杂。

---

## 二、基础设施层 (infra/)

基础设施层提供所有上层模块共用的能力：数据结构容器、错误追踪、日志、配置、Socket 封装、字节序转换。

### 2.1 容器模块 (`stl.c`)

提供四个泛型容器，全部使用 `void*` 实现类型擦除。调用方通过注入函数指针（`Comparator`、`HashFunction` 等）来定制类型行为。

#### 2.1.1 Vector（动态数组）

```
结构: T* elements, int size, int capacity
不变量: 0 <= size <= capacity, 有效元素区间 [0, size)
```

- **扩容策略**：当待插入元素数超过当前容量时，通过位左移（`<<= 1`）倍增容量，使用 `realloc` 原地扩展
- **插入**：使用 `for` 循环逐元素后移（`elements[i+1] = elements[i]`）
- **删除**：使用 `memmove` 批量前移，一次调用完成整个尾部搬迁
- 扩容失败时静默返回，不改变 vector 状态

#### 2.1.2 LinkedList（双向链表）

```
head ↔ node₁ ↔ node₂ ↔ ... ↔ tail
```

- `head = tail = NULL` 表示空链表
- 支持头插（`addFirst`）、尾插（`addLast`）、遍历（`foreach`）、按值删除（`remove`）、清空（`clear`）
- `remove` 通过 `Comparator` 判等，**删除所有匹配项**（遍历全程，不仅仅删除第一个）
- 释放时先清空所有节点再释放结构体本身

#### 2.1.3 HashMap（哈希表）

```
底层: Vector<LinkedList<HashMapEntry>>
算法: 链地址法
装填因子阈值: 0.75
初始桶数: 16
```

- `HashMapEntry` 持有 `K key` 和 `T data`，key 不拷贝——直接存储调用方传入的指针
- **哈希索引**：`abs(hash_function(key)) % bucket_count`
- **扩容**：装填因子超过 0.75 时桶数翻倍，所有 entry **重新散列**到新桶中。旧桶链表节点被释放（`linked_list_free`），但 entry 本体保留
- **put 幂等性**：key 已存在时覆盖 data 并返回 1，否则新增返回 0
- **remove**：通过 `KeyDestructor` 回调释放 key 持有的外部资源。直接从链表中摘节点（`O(1)` 链表操作），不需要全表搬移

预定义工具函数：
- `hash_func_uint64`：64 位整数哈希，使用 bit-mix 算法
- `hash_func_str`：DJB2 字符串哈希
- `compare_uint`、`compare_cstr`：整数和字符串比较器

#### 2.1.4 LazyHeap（懒删除优先队列）

```
底层: HeapElement*[] (1-indexed 最小堆) + HashMap (O(1) 元素定位)
扩容: 倍增策略
初始容量: 16
```

考虑一个场景：会话超时管理需要在优先队列中按时间戳排序，但当会话提前结束时需要从队列中移除中间元素。如果每次删除都在堆中做 O(n) 搜索和重建，代价太高。

**懒删除机制**：
- 删除时不立即移除元素，而是通过 HashMap 将对应 HeapElement 的 `is_deleted` 置为 1，并从 HashMap 中移除该条目
- 被标记的元素在比较逻辑中**永远小于**未删除元素（`is_deleted: -1 < 正常元素`），因此会逐渐上浮到堆顶
- `pop()` 和 `peek()` 时调用 `do_delete()`：循环检查堆顶是否为已删除元素，是则将其与末尾元素交换并下沉，直到堆顶为有效元素或队列为空

**比较规则**（`heap_element_compare`）：
- 两个已删除元素 → 相等（0）
- 已删除 vs 未删除 → 已删除更小（-1），即已删除元素视为"更优先"
- 两个未删除元素 → 调用 Comparator

**时间戳更新**的标准做法：先 `lazy_heap_remove`（标记旧条目删除）再 `lazy_heap_add`（以新时间戳入队），而非直接修改堆中元素（那样会破坏堆结构）。

### 2.2 异常追踪模块 (`exception.c`)

```
thread_local char msg[4096];        // 错误栈缓冲区（递减满栈）
thread_local char err_flag;         // 是否开启错误记录
thread_local int  stack_top;        // 栈顶索引（初始 4095）
```

**状态机语义**：

| 状态   | err_flag | stack_top      | catch() | 含义               |
| ----- | -------- | -------------- | ------- | ------------------ |
| close | 0        | 任意            | 0       | 未开启错误记录        |
| open  | 1        | = MSG_SIZE - 1 | 0       | 已开启，无错误        |
| err   | 1        | < MSG_SIZE - 1 | 1       | 已开启，有错误        |

**状态转移**：
- `close → open`：`ex_try()` 重置
- `open → err`：`ex_throw()` 写入错误信息
- `err → err`：`ex_throw()` 追加（多级调用链累积）
- `err/err → close`：`ex_end()` 消费错误信息并关闭

**调用约定**：
- **`void` 函数**：内部自开 try-catch-end 闭环，错误不污染上层
- **返回 `int` 的函数**：只 throw 不开 try，由调用者 catch-end；返回值负责控制流
- **`ex_throw()` 的安全防护**：若 `err_flag` 未开启，则自动调用 `ex_try()` 开启上下文
- 消息写入采用**递减满栈**：从缓冲区末尾向前写，防止栈溢出。每条消息以 `\n` 分隔

**设计意图**：这个模块解决的核心问题是——传统逐层打日志的方案会将一个完整错误的调用上下文拆成多条分散日志，在多线程下还会混杂。异常栈方案让每层通过 `ex_throw()` 累积位置信息，最终由顶层一次性获取完整调用链。

### 2.3 日志模块 (`logger.c`)

```
级别: TRACE < DEBUG < INFO < WARN < ERROR (共5级)
过滤: 通过配置中心读取 log_level，默认 INFO
输出: 各级别独立 FILE* 通道，可通过配置文件定向到 stdout/stderr/文件
```

- 配置解析器 `log_config_parser` 负责解析日志级别和各级别输出通道
- `do_log()` 在格式化内容前自动追加：单调时钟秒级时间戳 + 日志级别标签
- 每条日志后自动 `fflush` 确保输出
- 日志级别低于过滤级别时直接返回，无格式化开销
- 级别字符串匹配使用 `strcasecmp` 大小写不敏感

### 2.4 配置中心 (`config_center.c`)

#### 数据模型

```
configs_sections: HashMap<char*, ConfigSection*>
  └── ConfigSection
       └── entries: HashMap<char*, ConfigValue*>
            ├── is_cook: char       (是否已解析)
            └── value:   T          (原始字符串 或 解析后的值)
```

#### 延迟解析机制（策略模式）

由于 C 语言没有反射，配置中心无法在初始化时主动发现各模块的解析逻辑。解决方案是：

1. 各模块在自己的 `*_init()` 中通过 `config_register_parser(section, parser)` 注册解析函数到配置中心
2. `config_load_file()` 和 `config_inject()` 以**原始字符串**形式存储配置值（`is_cook = 0`）
3. 首次 `config_get()` 时检查 `is_cook` 标志：
   - 若为 0，调用对应 section 的 `ConfigParser` 解析
   - 解析成功后释放原始字符串，`value` 替换为解析结果，标记 `is_cook = 1`
4. 若解析器返回 -1（不支持该 key），保留原始字符串作为降级策略

#### 三种写入 API 的区别

| API              | is_cook | 用途                             |
| ---------------- | ------- | -------------------------------- |
| `config_load_file` | 0       | 从 INI 文件批量注入原始字符串       |
| `config_inject`    | 0       | 注入命令行参数等原始字符串          |
| `config_set`       | 1       | 直接设置已解析的值（如日志级别枚举）  |

#### 内存所有权与清理

- 解析前的原始字符串由 `strdup` 持有，解析成功后 `free`
- 若解析器分配了堆内存（如 `LinkedList<NetEnd*>`），模块需同时注册 `ConfigCleaner`
- `config_set` / `config_inject` 覆盖已有值时，会先调用 `ConfigCleaner`（如果 `is_cook=1`）或 `free`（如果 `is_cook=0`）释放旧值
- **因为 `HashMap` 以 `strdup` 的副本作为 key，所以 `hash_map_put` 覆盖时通过比较 key 值来判断是否需要 `free` 旧 key**

#### 文件解析（`config_load_file`）

使用逐行状态机：
- `[xxx]` → 提取 section 名，设置当前上下文
- `#` 开头 → 跳过（注释）
- 其他 → 解析 `key = value` 对，调用 `config_inject`

### 2.5 Socket 封装 (`sock_posix.c`)

#### 双栈设计

```c
socket(AF_INET6, SOCK_DGRAM, 0)
setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt=0, ...)  // 开启双栈
```

单个 IPv6 双栈 socket 可同时收发 IPv4 和 IPv6 数据包。IPv4 地址映射为 `::ffff:x.x.x.x`。

**收包时版本还原**：
- `IN6_IS_ADDR_V4MAPPED` 检测 → 若是 IPv4 映射地址，提取后 4 字节作为 `in_addr.ipv4`，设置 `version = IPV4`
- 否则直接复制 16 字节作为 `in_addr.ipv6`，设置 `version = IPV6`

**发包时协议选择**：
- 根据 `NetEnd.version` 分别构造 `sockaddr_in` 或 `sockaddr_in6`
- `NetEnd` 中的 `port` 和地址均为网络序

#### 地址抽象

```c
typedef union  { uint32_t ipv4; uint8_t ipv6[16]; } in_addr;
typedef struct { in_addr addr; IpVersion version; int port; /*网络序*/ } NetEnd;
```

#### 关键 API

- `socket_sleep_on()`：封装 `select()`，支持永不超时（`timeout < 0`）
- `socket_recv_nowait()`：非阻塞 `recvfrom()`，返回 0 表示无数据
- `ipstr2binary()`：使用 `inet_pton` 解析 IP 字符串为堆分配的 `NetEnd*`（端口默认 53）

#### 错误处理

所有系统调用错误通过 `ex_throw()` 记录 errno，不打印 stderr。上层通过 `ex_catch()`/`ex_end()` 获取。

### 2.6 工具函数 (`utils.c`)

#### 单调时钟

`sys_time_ms()` 使用 `CLOCK_MONOTONIC`，不受系统时间调整影响，适合超时计算和缓存 TTL 判断。

#### 字节序转换

CPU 端序仅检测一次，结果缓存在 `static` 变量中：

| 函数      | 字节数 | 实现                                  |
| --------- | ------ | ------------------------------------- |
| `h2n_2`   | 2      | `htons`                               |
| `h2n_4`   | 4      | `htonl`                               |
| `h2n_8`   | 8      | 小端时逐字节逆序，大端时无操作           |
| `h2n_16`  | 16     | 小端时逐字节逆序，大端时 memcpy         |

`n2h_*` 系列同理，使用 `ntohs`/`ntohl` 或相同的手动翻转逻辑。8 字节和 16 字节的转换使用自定义实现，因为 POSIX 不提供标准函数。

---

## 三、DNS 核心层 (dns/)

核心业务层负责 DNS 协议的序列化/反序列化、缓存管理、中继 ID 分配。本层是无状态设计（缓存模块除外），不持有运行时数据（或仅通过 `static` 持有单例状态）。

### 3.1 协议模块 (`dns_resolver.c`)

以 `DnsPacket` 结构为中枢的完整 DNS 协议实现。

```
DnsPacket
├── SectionHeader (12字节)
│   ├── id, flags        — 控制信息（ID、QR、OPCODE、RCODE 等）
│   ├── qcount           — 问题数量
│   └── answer_RRs, authority_RRs, additional_RRs — 各段 RR 计数
├── questions: Vector<SectionQuestion*>
└── rrs: Vector<ResourceRecord*> — 三段 RR 合并存储，按 answer/auth/add 顺序；
                                    用 header 中的计数字段逻辑分隔
```

#### 3.1.1 域名编解码

**编码** (`encode_name`，人可读 → DNS wire 格式)：
```
"www.baidu.com" → "\x03www\x05baidu\x03com\x00"
```
- 以 `.` 为分隔符扫描标签边界
- 每个标签前写入长度字节，标签体直接 memcpy
- 末尾写入 `\x00` 终止符
- 限制标签长度不超过 63 字节

**解码** (`decode_name`，DNS wire → 人可读格式)：
- 预扫描计算总长度（处理压缩指针直接返回 NULL——解码层不应遇到指针）
- 逐标签读取长度 + memcpy 标签体
- 标签间插入 `.`
- 末尾补 `\0`

#### 3.1.2 Header 的 Flags 字段

采用位运算宏操作 16 位 flags 字段，避免逐比特解析：

| 宏            | 位范围     | 说明                    |
| ------------- | --------- | ----------------------- |
| `IS_QUERY`    | bit 15    | QR：0=查询，1=响应       |
| `OPCODE_GET`  | bits 14-12| 操作码                  |
| `AA_GET/SET`  | bit 10    | 权威答案                |
| `TC_GET/SET`  | bit 9     | 截断标志                |
| `RD_GET/SET`  | bit 8     | 期望递归                |
| `RA_GET/SET`  | bit 7     | 递归可用                |
| `Z_GET/SET`   | bits 6-4  | 保留字段                |
| `RCODE`       | bits 3-0  | 响应码                  |

#### 3.1.3 序列化 / 反序列化

**字节流布局**：
```
[Header 12B] [Question Section] [Answer RRs] [Authority RRs] [Additional RRs]
```

**Header**：整体 `memcpy` 后，以 `uint16_t*` 遍历 6 个字段逐个调用 `n2h_2` 或 `h2n_2`

**Question**：name 为 DNS 编码字符串（`strcpy`/`strdup` + 越过 `\0`），qtype/qclass 各 2 字节需转换字节序

**RR**：name 支持**压缩指针**（以 `0xC0` 开头）。解析时递归处理指针：
- 扫描域名字段，找到指针标记（`& 0xC0 != 0`）
- 提取偏移量（低 14 位），递归解析指向的域名
- 拼接：前置标签 + 递归结果

**序列化时的指针压缩**（`name_serialize`）：
- 从 Header 之后开始搜索已输出的字节流
- 若找到相同的 name 字符串，使用 2 字节指针（`0xC000 | offset`）替代完整域名
- 从第一个标签头开始逐后缀搜索，优先找最长匹配

#### 3.1.4 包构造逻辑

**`pack_try_response_local()`**——本地应答决策树：

```
switch (OPCODE):
  case QUERY:
    → qcount 检查 (必须为 0 或 1，否则返回 NOTIMP)
    → 前置业务检查 query_pre_validate (当前为放行，预留封禁域名扩展)
    → 查缓存 dns_cache_get:
        ├─ 命中 → 构造标准响应 pack_make_std_response_local
        │         → 后置业务检查 query_post_validate
        │           (遍历 answer RR，若 rdata 全 0 则为拦截域名 → RCODE_NXDOMAIN)
        └─ 未命中 → 检查 RD 标志:
                     ├─ RD=1 → 返回 UPSTREAM (需转发)
                     └─ RD=0 → 返回空响应
  case IQUERY:
    返回 RCODE_NOTIMP (反向查询已弃用)
  case STATUS:
    返回 RCODE_NOTIMP (状态查询不支持)
  default:
    返回空响应
```

**`pack_make_query_relay()`**：克隆查询包 → 替换 ID 为中继 ID

**`pack_make_response_relay()`**：
1. 克隆上游响应包 → 替换 ID 为客户端 ID
2. 提取 RR 写入缓存（`dns_cache_put`），使后续相同查询走本地

**错误响应构造**：
- `make_response_fail()` → `make_response_empty()`：克隆问题段 + 设置对应 RCODE，不返回任何 RR
- `pack_make_inner_error()` → `make_response_fail(query, SERVFAIL)`

#### 3.1.5 日志序列化

`packet_to_log_string()` 将 DnsPacket 格式化到 `static char[4096]` 缓冲区，输出 Header 标志位、问题段、资源记录段。使用 `snprintf` 安全截断。

#### 3.1.6 rdata 解析

`rdata_deserialize()` 根据 RR 的 type 字段决定 rdata 的解析方式：

| Type   | rdata 处理                                                  |
| ------ | ----------------------------------------------------------- |
| CNAME  | `name_deserialize` 递归解码域名（含压缩指针展开）              |
| NS     | 同 CNAME                                                     |
| MX     | 先读 2 字节 preference（保持大端），再 `name_deserialize` 解码后续域名 |
| 其他   | `malloc` + `memcpy` 直接拷贝                                   |

序列化时 rdata 直接 `memcpy`（域名指针压缩仅在 RR 的 name 字段中使用，不在 rdata 中压缩）。

### 3.2 缓存模块 (`dns_cache.c`)

#### 3.2.1 数据结构

```
DnsCache (全局单例 g_cache)
├── table: HashMap<char*, CacheEntry*>   — O(1) 按键定位
├── head: CacheEntry*                    — LRU 哨兵 (不持有数据)
├── tail: CacheEntry*                    — LRU 哨兵 (不持有数据)
├── lock: mtx_t                          — 互斥锁
├── size: int                            — 当前条目数
└── capacity: int (默认 1024)             — 最大容量

CacheEntry (单条缓存)
├── key: char*           — "qclass|qtype|qname"，自有拷贝，key本身是dns域名编码字符串
├── value: CacheValue    — 内嵌，包含深拷贝的 RR 列表
├── created_at: ms       — 缓存写入时间（CLOCK_MONOTONIC 毫秒）
├── hotter: CacheEntry*  — LRU 更热方向
└── colder: CacheEntry*  — LRU 更冷方向

CacheValue
├── answer_RRs:    uint16_t
├── authority_RRs: uint16_t
├── additional_RRs:uint16_t
└── rrs: Vector<ResourceRecord*>  — 三个段的 RR 合并存储
```

**LRU 不变量**：
- `head->colder` 指向最热条目，`tail->hotter` 指向最冷条目
- 空链表：`head->colder = tail`，`tail->hotter = head`
- head/tail 为独立 `malloc` 分配的哨兵节点，不持有数据，所有插入/删除操作统一处理无需判空

#### 3.2.2 核心操作

**put (`dns_cache_put`)**：
1. 参数校验 → 构造 key（`"qclass|qtype|qname"`）
2. `mtx_lock` → HashMap 查找
3. **已存在**：深拷贝新 CacheValue → 释放旧 value.rrs → 更新 created_at → LRU 移至头部
4. **不存在**：容量满时淘汰 `tail->hotter`（最冷条目）→ 创建 CacheEntry（deep copy 所有 RR）→ `hash_map_put` + LRU 头插 → `size++`
5. `mtx_unlock`

**get (`dns_cache_get`)**：
1. 构造 key → `mtx_lock` → HashMap 查找
2. 过期检查：遍历 entry 中每条 RR，若 `(now - created_at) / 1000 >= rr->ttl` 则认为整条过期
   - `ttl == UINT32_MAX` 的 RR 永不触发过期（用于 IP 映射表中的记录）
3. 过期 → `cache_remove_entry` 删除 → 返回 miss
4. 命中 → 深拷贝所有 RR 到 result → LRU 移至头部 → 返回 hit

**prune (`dns_cache_prune`)**：
- 遍历 LRU 链表从 head 到 tail，对每条 entry 调用 `is_entry_expired()`
- 过期则 `cache_remove_entry`
- 每 4 秒由后台守护线程调用

**淘汰 (`cache_remove_entry`)**：
1. `lru_remove` 从链表中摘出
2. `hash_map_remove(entry->key, free)` — 传入 `free` 释放 HashMapEntry 持有的 key 的 `strdup` 副本
3. `cache_entry_free` — 释放 CacheEntry 的 key + value.rrs 中所有 RR + entry 本身

#### 3.2.3 内存所有权（key）

一个缓存条目涉及**两份**独立的 key 字符串拷贝：

| 持有者            | 来源                            | 释放方式                         |
| ----------------- | ------------------------------- | -------------------------------- |
| `HashMapEntry.key` | `strdup(CacheEntry.key)`         | `hash_map_remove(..., free)`     |
| `CacheEntry.key`   | `cache_key_create()` 中动态分配  | `cache_entry_free()` → `free`    |

**禁止共享指针**：`hash_map_put(map, entry->key, entry)` 是错误做法——这会让 HashMapEntry 和 CacheEntry 持有同一块 key 内存，任意一方释放后另一方持有野指针。正确做法是以 `strdup(entry->key)` 传入。

#### 3.2.4 IP 映射表加载 (`load_ip_table`)

`dnsrelay.txt` 格式：`域名=ip`，支持 `#` 注释。

解析流程：
1. 逐行读取，跳过空行和注释
2. 按 `=` 分割域名和 IP
3. `inet_pton` 判断 IP 类型（IPv4→A 记录，IPv6→AAAA 记录）
4. `rr_make_from_config_pair(domain, UINT32_MAX, type, ip)` 构造 RR——TTL 设为 `UINT32_MAX` 表示永不过期
5. 按 `"type|dns_encoded_name"` 聚合同域名的多条 RR
6. 每个聚合组调用 `dns_cache_put` 写入缓存

#### 3.2.5 线程安全

所有公开 API 通过 `mtx_lock`/`mtx_unlock` 保护。内部辅助函数（`_locked` 后缀）假定调用者已持锁。

锁不可重入。`put` 内部调用 `prune_locked` 时已持有锁，不会重复加锁。

### 3.3 ID 池模块 (`id_factory.c`)

#### 问题背景

DNS 协议通过 Header 中的 ID 字段匹配请求和响应。当中继转发时：
- 客户端发来查询包，携带 `client_id`
- 服务器分配 `relay_id`，发送到上游
- 上游响应携带 `relay_id`，服务器通过它找回客户端信息
- 最终返回给客户端的响应包使用 `client_id`

ID 空间为 16 位（0~65535），需要高效的分配和回收。

#### 实现方案

```c
static uint16_t ids[65536];   // 可用 ID 栈
static int      top;           // 栈顶索引（初始 65535）
static char     st[65536];     // 标记数组：是否已分配
```

- `id_pool_init()`：将 0~65535 全部推入 ids 数组，`top = 65535`，st 全清零
- `id_alloc(id)`：弹出栈顶 → 标记 `st[id] = 1` → `top--`
- `id_free(id)`：校验 `st[id] == 1` 且栈未满 → 标记清零 → `ids[++top] = id`

**特点**：
- O(1) 分配和回收，纯栈操作
- `char st[65536]` 仅占 64KB 标记空间
- 防止重复释放：`st[id]` 校验
- 无锁设计：ID 池仅在主线程中使用（后台守护线程不涉及 ID 操作）

---

## 四、服务层 (server/)

服务层负责事件调度、会话生命周期管理、后台周期性任务。

### 4.1 主循环 (`server.c`)

#### 4.1.1 初始化序列

```
server_start()
├── config_register_parser("server", server_config_parser)    // 注册解析器
├── config_register_cleaner("server", server_config_cleaner)  // 注册清理器
├── config_get → 读取超时时间、最大重试次数
├── config_get → 读取上游 DNS 服务器列表 (LinkedList<NetEnd*>)
├── thrd_create → 启动缓存 TTL 守护线程
├── init_socket → 创建 IPv6 双栈 socket + bind(53)
├── malloc 收发缓冲区 (recv_buf 1024B, send_buf 1024B)
└── server_loop() → 进入事件循环
```

**配置解析器** `server_config_parser`：
- `server_upstream`：逗号分隔的 IP 列表 → `LinkedList<NetEnd*>`，每个 `NetEnd` 由 `ipstr2binary` 解析，`malloc` 分配
- `dns_packet_timeout`：秒数 × 1000 → 毫秒 (`ms`)
- `max_retry_time`：`atol` → 整数
- 上游服务器不允许为本地地址（localhost/127.0.0.1/::1），防止自我收发死循环

#### 4.1.2 事件循环 (`server_loop`)

```
while (1):
    1. session_peek() → 获取最早超时的会话，计算 select 超时值:
        - 无等待会话 → timeout = -1 (永不超时)
        - 有等待会话 → timeout = request_timeout - elapsed
    
    2. socket_sleep_on(timeout) → select 阻塞
    
    3. select 返回:
        ├─ [有可读数据] 
        │   └─ while (pack_recv 读取到数据):
        │       ├─ handle_dns_packet(packet, source)
        │       └─ pack_free(packet)
        ├─ [超时]
        │   └─ batch_timeout() → 逐条检查超时会话
        └─ [select 错误]
            └─ 打印日志，退出循环
```

**关键设计**：select 的超时值不是固定值，而是动态推导自最早超时的会话。这样 CPU 不会忙等待，只在有数据到达或会话超时时才唤醒。

#### 4.1.3 数据包处理 (`handle_dns_packet`)

```
packet_in 是查询包?
├─ [是] 
│   ├─ pack_try_response_local() 
│   │   ├─ CLIENT → packet_send(响应包, 客户端) → pack_free
│   │   └─ UPSTREAM →
│   │        ├─ id_alloc(&relay_id)
│   │        │   └─ 耗竭 → 返回 SERVFAIL 错误响应
│   │        ├─ pack_make_query_relay() → 构造中继包
│   │        ├─ packet_send(中继包, pick_upstream())
│   │        │   └─ 发送失败 → id_free, pack_free, return
│   │        └─ session_open(client_id, client_addr, relay_packet)
│   │
│   └─
└─ [否 — 响应包]
    ├─ session_get(packet_in) → 查找对应会话
    │   ├─ pack_make_response_relay() → 构造客户端响应 + 缓存 RR
    │   ├─ packet_send(客户端响应, session->client_ip)
    │   ├─ session_close + id_free
    │   └─ pack_free
    └─ 未找到会话 → drop (可能是迟到的超时响应)
```

#### 4.1.4 超时处理 (`batch_timeout`)

```
while (session_peek() 已超时):
    取最早超时会话
    if (retry_times < max_retry_time):
        packet_send(中继包, pick_upstream())
        retry_times++
        session_wait() → 重新计时
    else:
        id_free + session_close → 事务终结
```

#### 4.1.5 上游负载均衡

`pick_upstream()` 使用**轮询**（Round-Robin）策略：
- 维护 `next_upstream` 指针，每次返回当前节点并前进到 `->next`
- 到达链表末尾时自然回到 `->head`（下次调用 `NULL → head`）

### 4.2 会话管理 (`session_factory.c`)

#### 4.2.1 数据结构

```c
Session
├── client_id: uint16_t        — 客户端查询 ID
├── client_ip: NetEnd           — 客户端地址（路由响应用）
└── relay_info: RelayInfo
     ├── timestamp: ms          — 上次发送转发包的时间戳
     ├── retry_times: char      — 已重试次数
     └── relay_packet: DnsPacket* — 缓存的中继包（重试用）
```

**双索引存储**：
```
agent_id_sessions: HashMap<uint16_t, Session*>
    (K = relay_id → V = Session*)
    用于：收到上游响应时，通过响应包的 ID 快速定位会话

sessions_queue: LazyHeap<Session*>
    (按 timestamp 排序的最小堆)
    用于：超时管理 —— 快速找到最早超时的会话
```

#### 4.2.2 关键操作

**`session_open()`**：
1. `malloc` Session → 设置 client_id、client_ip
2. `packet_clone(relay_packet)` → 深拷贝中继包（重试时复用）
3. `hash_map_put(agent_id_sessions, relay_id, session)`
4. `session_wait()` → 入队计时

**`session_wait()`**：
1. `lazy_heap_remove` → 从优先队列中标记旧时间戳对应的条目删除
2. 更新 `timestamp = sys_time_ms()`
3. `lazy_heap_add` → 以新时间戳重新入队

为什么这样做？因为如果直接修改堆中元素的时间戳，堆无法感知排序键值的变化，会破坏堆结构。标准做法是"删除旧 + 插入新"。

**`session_close()`**：
1. `hash_map_remove` → 从 agent_id_sessions 中移除
2. `lazy_heap_remove` → 标记删除
3. `pack_free(relay_packet)` → 释放中继包
4. `free(session)` → 释放 Session

**`session_get(relay_response)`**：以 `relay_response->header.id` 为 key 在 `agent_id_sessions` 中 O(1) 查找。

**`session_peek()`**：`lazy_heap_peek()` → 返回最早超时的会话（或 NULL）。

**`get_session_timeout_remain(session, timeout, &ms_out)`**：计算 `timeout - (now - timestamp)`。

### 4.3 守护线程 (`daemons.c`)

`daemon_dnscache_ttl()`:
- 在 `server_start()` 中通过 `thrd_create` 创建，`thrd_detach` 分离
- 每 4 秒调用 `dns_cache_prune()` 清理过期缓存
- 错误通过 `ex_try/ex_catch/ex_end` 自闭环处理

---

## 五、入口模块 (`main.c`)

```
main(argc, argv):
├── config_init()           → 创建配置中心的 data containers
├── param_get_config_file() → -c 参数指定配置文件路径
├── config_load_file()      → 加载 INI 配置文件 (原始字符串存入)
├── param_inject_config()   → -d/-dd 注入日志级别;
│                             位置参数拼接逗号后通过 config_inject 注入上游 IP
├── logger_init()           → 注册日志配置解析器 + 读取日志配置
├── id_pool_init()          → 初始化 ID 栈 [0..65535]
├── dns_cache_init()        → 创建 DnsCache 单例 + 加载 IP 映射表
├── session_factory_init()  → 创建会话存储 (HashMap + LazyHeap)
└── server_start()          → 注册 server 配置解析器/清理器 → 创建守护线程
                               → init socket → 进入事件循环 (不返回)
```

**初始化顺序有严格依赖**：配置中心必须最先初始化（其他模块依赖它读取配置），logger 其次（其他模块依赖日志输出），之后是各业务模块。

---

## 六、关键设计决策

### 6.1 单 Socket + select 事件驱动 vs 多线程

**选择**：单 socket + select 模型。

**理由**：
- 只有一个 UDP socket，无法并行收发，多线程网络 IO 无实际收益
- DNS 协议处理不涉及高复杂度算法，瓶颈在毫秒级网络延迟
- 避免了多线程并发访问共享状态（会话表、缓存、ID 池）的复杂度
- select 的超时值动态推导自最早超时的会话，CPU 不会空转

### 6.2 HashMap + LRU 双向链表复合索引

**选择**：缓存淘汰使用 HashMap 提供 O(1) 按键查找，LRU 双向链表提供 O(1) 的最近/最久访问定位。

**哨兵节点的设计价值**：
- head/tail 始终存在，所有插入/删除操作无需判空
- 空链表：`head->colder = tail`, `tail->hotter = head`
- 代码统一处理「插在 head 之后」「从 tail 之前删除」等边界情况

### 6.3 优先队列懒删除

**选择**：会话超时管理使用 LazyHeap（小顶堆+HashMap）。

**理由**：
- 避免 O(n) 的堆内搜索和重建
- 删除 O(1)：HashMap 标记 + 出队时 O(log n) 清理
- 时间戳更新：通过"删旧标记 + 重新入队"维护堆结构

### 6.4 配置延迟解析

**选择**：配置值仅在首次 `config_get()` 时解析，而非 `config_load_file()` 时全量解析。

**理由**：
- C 无反射，解析器需要在模块 init 时注册。延迟解析为注册提供了时机窗口
- 减少启动时的内存分配和解析开销
- 未被使用的配置项不产生解析开销
- 命令行参数和配置文件中的值走完全相同的延迟解析路径，维护成本减半

### 6.5 RR 全量深拷贝

**选择**：缓存模块对所有存入和返回的 RR 列表进行深拷贝。

**理由**：
- 缓存层和调用方内存完全隔离，无悬挂指针风险
- DNS 数据量小（通常 < 512 字节），拷贝开销可接受
- 实现简单可靠，避免复杂的内存所有权追踪

### 6.6 ID 池的无锁设计

**选择**：ID 池不加锁。

**理由**：ID 的分配和回收全部发生在主线程中（`handle_dns_packet`）。后台守护线程只做缓存 TTL 检查，不涉及 ID 操作。因此不需要同步。

---

## 七、数据流

### 7.1 客户端查询（缓存命中）

```
Client ──UDP──→ select √ → pack_recv → pack_deserialize
→ handle_dns_packet
  → pack_try_response_local
    → dns_cache_get (HashMap O(1) → LRU touch)
    → pack_make_std_response_local (deep clone RRs)
    → query_post_validate (检查是否为拦截 IP)
  → packet_send → pack_serialize → sendto
→ Client
```

### 7.2 客户端查询（需转发）

```
Client ──UDP──→ select √ → pack_recv → pack_deserialize
→ handle_dns_packet
  → pack_try_response_local → cache miss, RD=1 → UPSTREAM
  → id_alloc (栈弹 ID)
  → pack_make_query_relay (clone + 替换 ID)
  → packet_send → sendto
  → session_open (hash_map_put + lazy_heap_add)

  ... (等待上游响应) ...

← Upstream Response ←
  → pack_recv → pack_deserialize
  → handle_dns_packet
    → session_get (HashMap O(1))
    → pack_make_response_relay (clone + 替换 ID + dns_cache_put)
    → packet_send → sendto
    → session_close + id_free
→ Client
```

### 7.3 超时重试

```
select timeout →
→ batch_timeout
  → session_peek (LazyHeap peek)
  → get_session_timeout_remain ≤ 0?
    ├─ retry_times < max?
    │   → packet_send (重发中继包)
    │   → session_wait (lazy_heap_remove + lazy_heap_add 重新计时)
    └─ else
        → id_free + session_close (事务终结)
```

---

## 八、配置文件

### 8.1 `dnsrelay.ini`

```ini
[server]
server_upstream=10.3.9.6,10.3.9.4   # 逗号分隔的上游 DNS
dns_packet_timeout=1                  # 请求超时(秒)
max_retry_time=1                      # 最大重试次数

[dns]
iptable=./dnsrelay.txt                # IP-域名映射表路径

[log]
log_level=TRACE                       # TRACE|DEBUG|INFO|WARN|ERROR
TRACE=./debug.txt                     # 各级别日志可定向到文件
DEBUG=./debug.txt
```

### 8.2 `dnsrelay.txt`

```
# 拦截 (IP=0.0.0.0 返回 NXDOMAIN)
www.blocked.com=0.0.0.0

# 静态映射 (IPv4)
www.example.com=93.184.216.34

# 多 IP 同域名
www.example.com=93.184.216.35

# 静态映射 (IPv6)
localhost.example.com=::1
```

### 8.3 命令行

```
dnsrelay [-d|-dd] [-c config_path] [upstream_ip ...]
```

- `-d`：日志级别 = DEBUG
- `-dd`：日志级别 = TRACE
- `-c`：指定配置文件路径（默认 `./dnsrelay.ini`）
- 位置参数：上游 DNS 服务器 IP，多个空格分隔。内部用逗号拼接后通过 `config_inject` 注入，走相同的延迟解析路径
