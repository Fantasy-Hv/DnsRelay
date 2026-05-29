# DnsRelay 技术文档

## 一、项目概述

DnsRelay 是一个 DNS 中继服务器，基于 RFC 1035 标准实现。程序运行在本地 53 端口，拦截客户端的 DNS 查询请求，依据本地配置文件和缓存执行三级查询响应，并在本地无法应答时将请求转发至上游 DNS 服务器。

### 核心功能

| 检索结果           | 处理方式              | 功能定位      |
| -------------- | ----------------- | --------- |
| IP 地址为 0.0.0.0 | 返回"域名不存在"错误       | 不良网站拦截    |
| 配置文件中匹配到普通 IP  | 直接返回该地址           | 本地 DNS 服务 |
| 表中未检索到         | 转发至上游 DNS，结果返回客户端 | 中继功能      |

### 技术栈

- 语言：C (C23 标准)
- 构建：CMake 4.1+
- 平台：Linux (POSIX)
- 网络：UDP Socket，双栈模式 (IPv4/IPv6)
- 并发：`select()` 事件驱动 + 后台守护线程
- 线程库：`<threads.h>` (C11)

---

## 二、系统架构

### 分层设计

项目采用严格的分层架构，上层持有下层的数据结构并调用下层 API，下层不感知上层存在。每个模块在 `.c` 文件中维护自己的运行时状态（`static` 变量），对外表现为单例对象。

```
┌─────────────────────────────────────────┐
│              main.c (入口)              │
│         参数解析 / 模块初始化             │
├─────────────────────────────────────────┤
│            Server Layer (服务层)          │
│   server.c  │  session_factory.c  │ daemons.c
│   主事件循环  │    会话生命周期管理    │ 后台线程
├─────────────────────────────────────────┤
│            DNS Core Layer (核心业务层)     │
│   dns_resolver.c  │  dns_cache.c  │ id_factory.c
│   协议解析/构造    │   缓存管理     │  ID池
├─────────────────────────────────────────┤
│         Infrastructure Layer (基础设施层)  │
│  stl.c  │ config_center.c │ logger.c │ exception.c
│  容器    │    配置中心      │  日志     │  异常追踪
│  sock_posix.c │ utils.c
│  Socket封装   │ 字节序/时间
└─────────────────────────────────────────┘
```

### 依赖类型

- **数据型依赖**：上层通过配置中心读取配置；服务层通过配置获取上游服务器地址
- **接口型依赖**：上层调用下层 API（容器操作、Socket 操作、协议解析、日志打印）

### 并发模型

由于整个程序只有一个 UDP socket，无法并行收包/发包，因此采用单线程事件驱动模型：

- **主线程**：通过 `select()` 同时监听 socket 可读事件和会话超时，在单个线程内完成收包→处理→发包的全流程，避免多线程并发复杂度
- **后台守护线程**：独立执行缓存过期清理等周期性任务，通过互斥锁与主线程同步

---

## 三、基础设施层详解

### 3.1 容器模块 (stl)

**文件**：`include/infra/stl.h`, `src/infra/stl.c`

提供泛型数据容器，使用 `void*` 实现类型擦除。调用方通过 `Comparator`、`HashFunction` 等函数指针注入类型行为。

#### Vector（动态数组）

```
fields: elements, size, capacity
```

- 自动扩容：容量不足时倍增（bit shift），使用 `realloc` 原地扩展
- 支持尾部追加 (`add`) 和任意位置插入 (`insert`)，插入时使用 `memmove` 批量搬移元素
- 删除使用 `memmove` 覆盖被删元素，避免内存碎片

#### LinkedList（双向链表）

```
head ↔ node1 ↔ node2 ↔ ... ↔ tail
```

- 支持头插 (`addFirst`)、尾插 (`addLast`)、遍历 (`foreach`)、按值删除 (`remove`)
- `remove` 通过 `Comparator` 判断元素相等，支持删除所有匹配项
- 内部使用 `LinkedNode` 结构，持有 `prev`/`next` 指针

#### PriorityQueue（最小堆优先队列）

```
底层: HeapElement[]（1-based 索引）
扩容: 倍增策略
```

- **懒删除机制**：删除元素时不立即移除，而是标记 `is_deleted = 1`。被标记元素在比较时永远小于未删除元素（保证上溢到堆顶），在 `pop`/`peek` 时批量清理堆顶的已删除元素
- 比较规则：两个已删除元素相等 (返回 0)；已删除 < 未删除 (返回 -1)；否则调用 `Comparator`
- 堆操作：`up` (上浮)、`down` (下沉)、`lazy_delete` (清理堆顶已删除元素)

#### HashMap（哈希表）

```
结构: Vector<LinkedList<HashMapEntry>>
算法: 链地址法
扩容: 装填因子 > 0.75 时扩容为 2 倍
```

- `HashMapEntry` 持有 key 和 data，key 由容器持有（调用方传入的指针），不拷贝
- 哈希函数结果取绝对值后对桶数取模得到桶索引
- 扩容时创建新桶数组，将所有 entry 重新散列到新桶，释放旧桶链表节点但保留 entry
- `put` 具有幂等性：如果 key 已存在则覆盖 data 并返回 1，否则新增并返回 0
- `remove` 通过 `KeyDestructor` 回调释放 key 持有的外部资源

预定义的比较/哈希函数：

- `hash_func_uint16` / `compare_uint16`：用于 `uint16_t` 类型 key（直接强转为 int）
- `hash_func_str` / `compare_cstr`：用于字符串 key（DJB2 哈希算法，`strcmp` 比较）

---

### 3.2 异常追踪模块 (exception)

**文件**：`include/infra/exception.h`, `src/infra/exception.c`

设计目标：将**控制信息**（该不该继续）和**诊断信息**（出问题看什么）解耦。返回值只负责控制流，异常上下文负责累积诊断信息。

#### 核心 API

| API                 | 作用              | 调用者      |
| ------------------- | --------------- | -------- |
| `ex_try()`          | 重置上下文，开启新一轮错误记录 | 关心错误的父函数 |
| `ex_throw("位置、详情")` | 向上下文追加诊断信息      | 出错的子函数   |
| `ex_catch()`        | 判断是否有错 (返回 1/0) | 父函数      |
| `ex_end()`          | 取走完整诊断链并重置上下文   | 父函数      |

#### 实现原理

基于 `thread_local` 的隐式错误栈：

```c
thread_local char msg[4096];       // 调用栈缓冲
thread_local char err_flag = 0;    // 是否开启记录
thread_local int stack_top = 4095; // 递减满栈（从尾部向前写）
```

- `ex_try()`：设置 `err_flag = 1`，`stack_top` 复位到缓冲区末尾
- `ex_throw()`：将格式化字符串写入栈顶（向前增长），每条信息以 `\n` 分隔。如果 `err_flag` 为 0 则自动调用 `ex_try()`
- `ex_catch()`：`err_flag && stack_top < 4095`（有开启记录且有内容）
- `ex_end()`：返回错误栈字符串指针，设置 `err_flag = 0`（消费语义）

#### 调用约定

- **`void` 函数**：内部自己 try-catch-end 闭环，错误不污染上层
- **返回 `int` 的函数**：只 throw 不开 try，由调用者 catch-end；返回值负责控制流
- **嵌套安全**：父开了 try 表明关心子函数，子函数不应自己再 try。`void` 子函数自开 try 属于独立作用域，不影响父。父子的try-end块不交叉

#### 设计优势

- 一个错误的完整调用路径是一条日志（通过多层 `ex_throw` 累积），而非多条分散日志
- `try` 即重置，`end` 即消费，无复杂生命周期管理
- `thread_local` 保证多线程环境下的线程安全

---

### 3.3 日志模块 (logger)

**文件**：`include/infra/logger.h`, `src/infra/logger.c`

提供分级日志打印 API，被所有其他模块依赖。

#### 日志级别

```
TRACE < DEBUG < INFO < WARN < ERROR
```

- `TRACE`：跟踪程序运行细节
- `DEBUG`：调试信息
- `INFO`：程序运行的关键节点
- `WARN`：警告，可能导致错误
- `ERROR`：错误信息

#### 实现细节

- 日志过滤级别通过配置中心 读取`log_level` ，默认为 `INFO`
- 各级别输出流向用 `output_channels[level]` 数组管理，并且可以通过配置文件指定
- `do_log()` 在格式化内容前自动追加时间戳（单调时钟毫秒→秒）和日志级别标签
- 配置解析器 `log_config_parser` 通过字符串大小写不敏感匹配解析日志级别

---

### 3.4 配置中心 (config)

**文件**：`include/infra/config.h`, `src/infra/config_center.c`

负责读取和持有应用的配置信息，通过 `static` 变量存储运行时状态。

#### 配置文件格式

不支持行尾注释

```ini
# comment 
[section1]
key1 = value2
key2 = value2

[section2]
...
```

#### 数据模型

```
configs_sections: HashMap<char*, ConfigSection*>
  └── ConfigSection
       └── entries: HashMap<char*, ConfigValue*>
            ├── is_cook: 是否已解析
            └── value: 解析前的原始字符串 或 解析后的值
```

#### 延迟解析机制

配置项的解析采用懒加载策略：

1. `config_load_file()` 读取时以原始字符串形式存储（`config_inject`）
2. 首次 `config_get()` 时检查 `is_cook` 标志，若未解析则调用注册的 `ConfigParser`
3. 解析成功后释放原始字符串，将 `value` 替换为解析结果，标记 `is_cook = 1`

#### 配置解析器注册

由于 C 没有反射，各模块需在自己的初始化函数中注册配置解析器：

```c
config_register_parser("section_name", my_parser);
config_register_cleaner("section_name", my_cleaner); // 可选，用于释放解析器分配的内存
```

解析器签名：`int (*ConfigParser)(const char* key, const char* value, T* result)`

- 返回 0 表示解析成功，-1 表示失败（降级保留原字符串）

#### API 分类

| API                | 用途              | 值的形态               |
| ------------------ | --------------- | ------------------ |
| `config_set`       | 直接设置解析后的值       | `is_cook = 1`      |
| `config_inject`    | 注入原始字符串（如命令行参数） | `is_cook = 0`（待解析） |
| `config_load_file` | 从配置文件批量注入       | `is_cook = 0`（待解析） |

#### 文件解析

使用逐行状态机：

- 遇到 `#` → 跳过（注释）
- 遇到 `[` → 提取 section 名，设置当前 section 上下文
- 否则 → 解析 `key = value` 对，调用 `config_inject`

---

### 3.5 Socket 封装 (sock_posix)

**文件**：`include/infra/socket.h`, `src/infra/sock_posix.c`

对 POSIX socket 系统调用的薄封装层。

#### 核心设计

- 创建 IPv6 双栈 socket：`socket(AF_INET6, SOCK_DGRAM, 0)` + `setsockopt(IPV6_V6ONLY, 0)`
  - 双栈模式下单个 socket 可同时收发 IPv4 和 IPv6 数据包（IPv4 地址映射为 `::ffff:x.x.x.x`）
- 收包时通过 `IN6_IS_ADDR_V4MAPPED` 判断来源 IP 版本，自动将 IPv4 映射地址还原

#### 地址抽象

```c
typedef union { uint32_t ipv4; uint8_t ipv6[16]; } in_addr;

typedef struct {
    in_addr addr;
    IpVersion version; // 自定义枚举类
    int port; // 网络序
} NetEnd;
```

#### 关键 API

- `socket_create()` / `socket_bind()` / `socket_release()`：生命周期管理
- `socket_send()`：根据 `NetEnd.version` 构造 `sockaddr_in` 或 `sockaddr_in6` 后调用 `sendto()`
- `socket_recv_nowait()`：非阻塞 `recvfrom()`，返回 0 表示无数据
- `socket_sleep_on()`：封装 `select()`，阻塞等待 socket 可读或超时
- `ipstr2binary()`：使用 `inet_pton` 将 IP 字符串转为 `NetEnd*`（堆分配，默认端口 53）

---

### 3.6 工具函数 (utils)

**文件**：`include/infra/utils.h`, `src/infra/utils.c`

#### 单调时钟

```c
ms sys_time_ms(void); // 返回 CLOCK_MONOTONIC 毫秒时间戳
```

使用 `CLOCK_MONOTONIC` 而非 `CLOCK_REALTIME`，不受系统时间调整影响，适合超时计算。

#### 字节序转换

提供统一的主机序↔网络序转换接口，内部自动检测 CPU 端序：

| 函数                  | 字节数 | 实现                |
| ------------------- | --- | ----------------- |
| `h2n_2` / `n2h_2`   | 2   | `htons` / `ntohs` |
| `h2n_4` / `n2h_4`   | 4   | `htonl` / `ntohl` |
| `h2n_8` / `n2h_8`   | 8   | 小端时手动翻转           |
| `h2n_16` / `n2h_16` | 16  | 小端时逐字节逆序          |

端序检测通过 `static` 变量缓存结果，仅首次调用时检查。

---

## 四、DNS 核心层详解

### 4.1 协议模块 (dns_resolver)

**文件**：`include/dns/protocol.h`, `src/dns/dns_resolver.c`

负责 DNS 数据包的序列化、反序列化以及包的构造与转换。本模块为无状态设计，不持有运行时数据。

#### 数据模型

```
DnsPacket
├── SectionHeader (12 字节固定报头)
│   ├── id, flags (控制+状态)
│   ├── qcount (问题数)
│   ├── answer_RRs, authority_RRs, additional_RRs
├── questions: Vector<SectionQuestion*>
├── rrs: Vector<ResourceRecord*> (三个段的RR,通过头部的计数字段进行逻辑分隔)
```

#### Header Flags 解析

采用位运算宏操作 16 位 flags 字段：

| 宏            | 位范围        | 说明              |
| ------------ | ---------- | --------------- |
| `IS_QUERY`   | bit 15     | QR 标志：0=查询，1=响应 |
| `OPCODE_GET` | bits 14-12 | 操作码             |
| `AA_GET/SET` | bit 10     | 权威答案            |
| `TC_GET/SET` | bit 9      | 截断标志            |
| `RD_GET/SET` | bit 8      | 期望递归            |
| `RA_GET/SET` | bit 7      | 递归可用            |
| `Z_GET/SET`  | bits 6-4   | 保留字段            |
| `RCODE`      | bits 3-0   | 响应码             |

#### 序列化 / 反序列化

**字节流布局**：

```
[Header 12B] [Question Section] [Answer RRs] [Authority RRs] [Additional RRs]
```

- Header：整体 `memcpy` 后逐字段进行字节序转换（6 个 uint16_t）
- Question：name 为 DNS 编码字符串（以 `\0` 结尾），qtype/qclass 各 2 字节
- RR：name 支持压缩指针（以 `0xc0` 开头），后面依次为 type(2B) + class(2B) + ttl(4B) + rdlength(2B) + rdata(variable)
- 序列化时实现**域名指针压缩**：从 Header 之后扫描已输出的字节，若发现相同的 name 字符串，使用 2 字节指针（`0xc000 | offset`）替代完整域名

#### RR 构造函数

| 函数                       | 用途                                              |
|--------------------------|-------------------------------------------------|
| `rr_create()`            | 分配空白 ResourceRecord                             |
| `rr_clone(record)`       | 深拷贝一条 RR（name 通过 strdup，rdata 通过 malloc+memcpy） |
| `rrs_clone(Vector* rrs)` | 批量深拷贝 RR（name 通过 strdup，rdata 通过 malloc+memcpy） |

#### 包构造逻辑

**`pack_try_response_local()` — 本地应答决策**：

```
switch (OPCODE):
  case QUERY:
    1. 检查 qcount（必须为 0 或 1，否则返回 NOTIMP）
    2. 前置业务检查 (query_pre_validate)
    3. 查缓存 (dns_cache_get)
       ├── 命中 → 构造标准响应 (pack_make_std_response_local)
       │          └── 后置业务检查 (query_post_validate: 拦截 0.0.0.0)
       └── 未命中 → 检查 RD 标志
                    ├── RD=1 → 返回 UPSTREAM（需转发）
                    └── RD=0 → 返回空响应
  case IQUERY: 返回 NOTIMP
  case STATUS: 构造状态响应 (make_response_status)
```

**`pack_make_query_relay()`**：克隆查询包，替换 ID 为中继 ID

**`pack_make_response_relay()`**：克隆上游响应，替换 ID 为客户端 ID，同时提取 rr 写入缓存

#### 错误响应构造

- `make_response_empty()`：克隆问题段，设置 RA 标志，answer/auth/add 计数为 0
- `make_response_fail()`：基于空响应设置对应 RCODE
- `pack_make_inner_error()`：构造 SERVFAIL 响应

---

### 4.2 缓存模块 (dns_cache)

**文件**：`include/dns/cache.h`, `src/dns/dns_cache.c`

负责 DNS 资源记录的缓存管理，线程安全的全局单例。采用 **HashMap + LRU 双向链表** 组合，实现 O(1) 存取和淘汰以及O(n)过期检查。

#### 缓存键

以查询三元组 `(qname, qtype, qclass)` 构成唯一键：

```
键格式: "qclass|qtype|qname"
示例: "1|1|www.example.com"
```

#### 数据结构

```c
CacheValue (协议层与缓存层的数据交互模型)
├── answer_RRs: uint16_t     // Answer 段 RR 数量
├── authority_RRs: uint16_t  // Authority 段 RR 数量
├── additional_RRs: uint16_t // Additional 段 RR 数量
└── rrs: Vector<ResourceRecord*> // 所有 RR（按 answer/auth/add 顺序排列）

CacheEntry (单条缓存条目)
├── key: char*               // 格式 "qclass|qtype|qname"（自有拷贝）
├── value: CacheValue         // 内嵌，存放深拷贝后的 RR 列表
├── created_at: ms            // 缓存写入时刻（单调毫秒时间戳）
├── hotter: CacheEntry*       // LRU 更热（更近被访问）指针
└── colder: CacheEntry*       // LRU 更冷（更久未被访问）指针

DnsCache (全局单例 g_cache)
├── table: HashMap<char*, CacheEntry*>  // O(1) 按键定位
├── head: CacheEntry*                   // LRU 哨兵节点（malloc，不持有数据）
├── tail: CacheEntry*                   // LRU 哨兵节点（malloc，不持有数据）
├── lock: mtx_t                         // 互斥锁，保证线程安全
├── size: int                           // 当前条目数
└── capacity: int (默认 1024)
```

**LRU 不变量**：
- `head->colder` 指向最热条目，`tail->hotter` 指向最冷条目
- 空链表时：`head->colder = tail`，`tail->hotter = head`
- 哨兵为独立 `malloc` 分配，不存储实际数据，边界操作无需判空

#### 核心操作

**`dns_cache_put(qname, type, class, cache_value)`**：

1. 参数校验：`qname == NULL` 或 `cache_value.rrs == NULL` 返回 1
2. 构造查询键 `"class|type|qname"`
3. 加锁 → 先执行 `prune_locked()` 清理过期条目腾空间
4. 查找是否已有该 key 的条目：
   - **已存在** → 释放旧 `value.rrs`，深拷贝新 CacheValue，更新 `created_at`，LRU 移至头部
   - **不存在** → 容量检查（`size >= capacity` 时淘汰 `tail->hotter` 最冷条目）→ 创建 CacheEntry（深拷贝所有 RR + 记录时间戳）→ HashMap 插入 + LRU 头插 → `size++`
5. 解锁 → 返回 0

**`dns_cache_get(qname, type, class, result)`**：

1. 参数校验 → 构造键 → 加锁
2. HashMap 查找 CacheEntry → 未找到返回 miss (1)
3. 过期检查：遍历 `entry->value.rrs` 中每条 RR，若 `(now - created_at) / 1000 >= rr->ttl` 则整条过期 → 删除 entry → 返回 miss
   - `ttl == UINT32_MAX` 的 RR 永不触发过期条件（用于 IP 映射表）
4. 命中 → 深拷贝所有 RR 到 `result`（`rr_clone` 拷贝，保留原始 TTL）→ LRU 移至头部 → 解锁 → 返回 hit (0)

**`dns_cache_prune()`**：遍历 LRU 链表 `head → ... → tail`，对每个条目调用 `is_entry_expired()`，已过期则 `cache_remove_entry()`（同时清理 HashMap 和 LRU 链表引用并释放内存）。

**LRU 条目淘汰**：
- 容量满时，取 `tail->hotter`（最冷条目）执行 `cache_remove_entry`
- **LRU 位置更新**规则：
  - `put` 新条目 → 头插到 `head` 之后
  - `put` 覆盖已有条目 → 摘出后头插（写入视为新访问）
  - `get` 命中 → 摘出后头插

#### 内存所有权

一个缓存条目涉及两份独立的 key 拷贝：

| 持有者            | 来源                            | 何时释放                        |
| --------------- | ----------------------------- | --------------------------- |
| `HashMapEntry.key` | `strdup(CacheEntry.key)`      | `hash_map_remove(..., free)` |
| `CacheEntry.key`   | `cache_key_create()` 中动态分配   | `cache_entry_free()`         |

**禁止共享指针**：`hash_map_put(map, entry->key, entry)` 会使双方指向同一内存，任意方释放后另一方持有野指针。

#### 线程安全

所有公开 API 通过 `mtx_lock`/`mtx_unlock` 保护 `g_cache` 的访问：

| 操作                       | 锁策略              |
| ------------------------ | ---------------- |
| `dns_cache_put`          | 全程持锁（内部调 prune_locked） |
| `dns_cache_get`          | 全程持锁（clone + LRU 移动） |
| `dns_cache_prune`        | 加锁 → prune_locked → 解锁 |
| `dns_cache_load_ip_table` | 内部每次 put 自动加锁      |
| `dns_cache_free`         | 加锁遍历释放 → 解锁销毁      |

---

### 4.3 ID 池模块 (id_factory)

**文件**：`include/dns/id.h`, `src/dns/id_factory.c`

管理 DNS 中继转发的 16 位 ID 分配与回收。

#### 问题背景

DNS 协议通过 ID 字段匹配请求和响应。中继转发时，发往上游的查询使用服务器分配的 relay_id，收到响应后通过 relay_id 找回客户端真实 ID。ID 空间有限（0~65535），需要高效分配回收。

#### 实现方案

```c
static uint16_t ids[65536];   // 可用 ID 栈
static int top;                // 栈顶索引（初始值 65535）
static char st[65536];         // 标记数组：ID 是否已分配
```

- `id_pool_init()`：将 0~65535 依次填入 ids 数组，`top = 65535`，st 全清零
- `id_alloc(id)`：弹出栈顶 ID，标记 `st[id] = 1`，`top--`
- `id_free(id)`：检查 `st[id] == 1` 且栈未满，标记清零，`ids[++top] = id`

#### 设计特点

- **O(1) 分配/回收**：栈操作，无搜索开销
- **单字节标记数组**：`char st[65536]` 仅占 64KB，相比 `uint16_t` 数组节省空间
- **安全回收**：通过 `st[id]` 校验防止重复释放
- 无锁设计：ID 池仅在主线程中使用（后台守护线程不涉及 ID 操作）

---

## 五、服务层详解

### 5.1 Server 主循环 (server.c)

**文件**：`include/server/server.h`, `src/server/server.c`

DNS 服务器的核心调度模块，负责事件循环、包收发调度和上游负载均衡。

#### 主循环流程

```
server_start()
  ├── 注册服务器配置解析器/清理器
  ├── 读取配置（超时、重试次数、上游列表）
  ├── 初始化 socket（创建+绑定）
  ├── 分配收发缓冲区
  └── server_loop()
       └── while (1):
            ├── session_peek() → 获取最早超时的会话
            ├── socket_sleep_on(timeout) → select 阻塞
            ├── [无错误] 循环收包 (pack_recv)
            │   ├── 无数据 → break
            │   ├── 收包错误 → 打日志 break
            │   └── 收到包 → handle_dns_packet(packet, source)
            └── [select 错误] → 打日志 break
```

**select 超时值**由最早超时会话决定：若无等待中的会话，永不超时（`-1`）；否则为 `request_timeout - elapsed`。

#### 数据包处理 (handle_dns_packet)

```
packet_is_query(packet_in)?
  ├── [是查询包]
  │   ├── pack_try_response_local() → 本地可应答?
  │   │   ├── [CLIENT] → packet_send → pack_free
  │   │   └── [UPSTREAM]
  │   │        ├── id_alloc(&relay_id)
  │   │        │   └── [耗竭] → 返回 SERVFAIL
  │   │        ├── pack_make_query_relay() 构造中继包
  │   │        ├── packet_send() 发往上游
  │   │        ├── [发送失败] → id_free, pack_free
  │   │        └── [发送成功] → session_open()
  │   └──
  └── [是响应包]
      ├── session_get(packet_in) → 找到会话
      │   ├── pack_make_response_relay() 构造客户端响应
      │   ├── packet_send() 返回客户端
      │   ├── session_close() + id_free()
      │   └── pack_free()
      └── [未找到] → drop（可能是迟到响应）
```

#### 超时处理 (batch_timeout)

```
while (session_peek() 已超时):
    if (retry_times < max_retry):
        packet_send → retry_times++
        session_wait → 重新计时
    else:
        id_free + session_close → 事务终结
```

#### 上游负载均衡

`pick_upstream()` 使用轮询（Round-Robin）策略：

- 维护 `next_upstream` 指针，每次调用返回当前节点并前进
- 当指针到达链表末尾时，下次调用重新从头开始

#### 配置解析器

`server_config_parser()` 解析三类配置：

- `server_upstream`：逗号分隔的 IP 列表 → `LinkedList<NetEnd*>`
- `dns_packet_timeout`：请求超时秒数 → `ms`
- `max_retry_time`：最大重试次数 → `int`

---

### 5.2 会话管理 (session_factory)

**文件**：`include/server/session.h`, `src/server/session_factory.c`

管理中继请求的生命周期，提供超时驱动的会话追踪。

#### 数据结构

```c
Session
├── client_id: uint16_t        // 客户端查询 ID
├── client_ip: NetEnd          // 客户端地址（用于响应路由）
└── relay_info: RelayInfo
     ├── timestamp: ms         // 上次发送转发包的时间
     ├── retry_times: char     // 已重试次数
     └── relay_packet: DnsPacket*  // 缓存的中继包（重试用）
```

#### 双索引存储

```
agent_id_sessions: HashMap<uint16_t, Session*>
    └── 按 relay_id 快速查找会话（用于匹配上游响应）

sessions_queue: PriorityQueue<Session*>
    └── 按 timestamp 排序的最小堆（用于超时管理）
```

#### 关键操作

**`session_open()`**：

1. 分配 Session，设置 client_id、client_ip
2. 深拷贝中继包到 `relay_info.relay_packet`
3. 以 relay_id 为 key 存入 `relay_id_sessions`
4. 调用 `session_wait()` 启动计时

**`session_wait()`**：

1. 更新 `timestamp = sys_time_ms()`
2. 先将旧引用从优先队列中标记删除（`priority_remove`）
3. 再以新 timestamp 重新入队（`priority_queue_add`）
4. 通过"删旧+加新"而非直接修改，确保堆结构正确

**`session_close()`**：

1. 从 `relay_id_sessions` 中移除
2. 从 `sessions_queue` 中懒删除
3. 释放中继包副本和 Session 本身

**`session_get(relay_response)`**：以响应的 `header.id` 为 key 在 `relay_id_sessions` 中查找

**`session_peek()`**：返回优先队列堆顶元素（最早超时的会话）

---

### 5.3 守护线程 (daemons)

**文件**：`include/server/daemon.h`, `src/server/daemons.c`

独立于主事件循环的后台线程。

**`daemon_dnscache_ttl()`**：

- 每 4 秒苏醒一次
- 调用 `dns_cache_prune()` 清理过期缓存
- 通过异常追踪机制记录错误

---

### 5.4 入口 (main.c)

**文件**：`src/main.c`

程序入口，负责初始化和编排。

#### 命令行参数

```
dnsrelay [-d | -dd] [-c <config_file>] [upstream_ips...]
```

| 参数          | 说明                                                  |
| ----------- | --------------------------------------------------- |
| `-d`        | 日志级别设为 DEBUG                                        |
| `-dd`       | 日志级别设为 TRACE                                        |
| `-c <path>` | 指定配置文件路径（默认 `./dnsrelay.txt`）                       |
| 位置参数        | 上游 DNS 服务器 IP，多个以空格分隔，内部用逗号拼接后通过 `config_inject` 注入 |

#### 初始化序列

```
main()
  ├── config_init()            → 初始化配置中心的数据容器
  ├── param_get_config_file()  → 获取配置文件路径
  ├── config_load_file()       → 加载配置文件
  ├── parse_param()            → 注入命令行参数到配置系统
  ├── logger_init()            → 注册日志配置解析器 + 读取日志配置
  ├── id_pool_init()           → 初始化 ID 栈
  ├── dns_cache_init()         → 创建缓存单例（HashMap + LRU 双向链表 + 互斥锁）+ 加载ip映射表
  ├── session_factory_init()   → 创建会话存储（HashMap + PriorityQueue）
  └── server_start()           → 进入主事件循环
```

---

## 六、关键设计决策

### 6.1 单 Socket 事件驱动 vs 多线程

选择单 socket + select 模型而非多线程：

- 只有一个 UDP socket，无法并行收发，多线程网络 IO 无收益
- DNS 协议处理不涉及高复杂度算法，瓶颈在毫秒级网络延迟
- 避免了多线程并发访问共享状态（会话表、缓存、ID 池）的复杂度

### 6.2 错误处理：返回值 + 异常栈

不采用逐层打日志的传统方式，而是分离控制流和诊断流：

- **控制流**：通过函数返回值（int），只管"该不该继续"
- **诊断流**：通过隐式 thread_local 栈，在错误发生时逐层累积上下文，最终由顶层一次性输出完整调用链
- 一个错误的完整调用路径是一条日志，而非多条分散日志

### 6.3 配置模块的策略模式与延迟解析

配置值首次读取时才解析，而非加载时全量解析：

- 为上层解析器的注册提供时机
- 减少启动时的内存分配
- 未被使用的配置项不会产生解析开销
- 命令行注入的原始字符串和配置文件中的字符串统一走延迟解析流程

配置中心的设计核心在于将"配置数据的存储/读取"与"配置值的解析"解耦：

- **数据流**：配置文件 / 命令行参数 → 原始字符串（`config_inject` / `config_load_file`）→ 延迟解析（`config_get` 首次访问）→ 解析后的值
- **解析器注册**：由于 C 没有反射，配置中心无法主动发现各模块的解析逻辑。因此采用"上层注册、下层回调"的策略模式：各模块在 `*_init()` 中通过 `config_register_parser(section, parser)` 将解析函数注册到配置中心。当配置值首次被 `config_get` 访问时，配置中心查找对应 section 的解析器执行解析
- **内存所有权**：解析前的原始字符串由配置中心通过 `strdup` 持有；解析成功后释放原字符串，替换为解析器分配的新值（可能是指针类型）。若解析器分配了堆内存，模块需同时注册对应的 `ConfigCleaner`，供配置更新时回收旧值
- **命令行注入**：命令行参数通过 `config_inject` 以原始字符串形式注入，与配置文件中的值走完全相同的延迟解析路径，避免了两套解析逻辑的维护成本
- **降级策略**：若解析器返回 -1（不支持该 key），配置中心保留原始字符串，由上层自行处理

### 6.4 优先队列懒删除

会话超时管理中使用 懒删除堆（小顶堆+哈希表）：

- 避免 O(n) 的堆内搜索和重建
- 删除操作 O(1) 标记 + 出队时 O(log n) 清理，效率更高
- 通过"删旧标记 + 重新入队"方式更新会话时间戳

### 6.5 RR 深拷贝的缓存策略

缓存模块对所有存入的 RR 进行深拷贝：

- 缓存层和调用方内存完全隔离，无悬挂指针风险
- 返回给客户端时再次深拷贝（`rr_clone`），保证数据一致性
- 代价是额外的 `malloc`/`memcpy`，但 DNS 数据量小（通常 < 512 字节），可接受

### 6.6 LRU 双向链表 + HashMap 组合索引

缓存淘汰采用 LRU 策略而非简单的 LinkedList 遍历：

- HashMap 提供 O(1) 的 key 查找，LRU 双向链表提供 O(1) 的"最热/最冷"位置操作
- 哨兵节点（head/tail）避免空链表和边界 case 的判空判断，所有插入/删除操作统一处理
- prune 过期清理直接遍历 LRU 链表（从最热到最冷），无需 HashMap 遍历，复杂度 O(n)
- 淘汰时取 `tail->hotter` 即可定位最冷条目，O(1)

## 七、配置参考

### 完整配置示例 (dnsrelay.ini)

```ini
[server]
server_upstream=10.3.9.6
dns_packet_timeout=1
max_retry_time=1
[dns]
iptable=./dnsrelay.txt
[log]
log_level=TRACE
DEBUG=./debug.txt
TRACE=./debug.txt
```

