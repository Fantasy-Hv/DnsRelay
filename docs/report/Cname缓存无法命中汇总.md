# DNS 查询键与资源记录键不一致导致的缓存失配问题

## 问题背景

本项目的 DNS 中继服务在收到上游 DNS 服务器返回的响应后，会将结果写入本地缓存，以便后续相同查询可以直接命中缓存，减少再次访问上游服务器的开销。

在开发过程中，我们发现：当一个查询的响应中包含 `CNAME RR` 跳转关系时，虽然上游已经返回了完整答案，但后续对同一问题的重复查询仍然会出现缓存未命中的现象。进一步分析后发现，这并不是简单的“缓存缺失”，而是 **DNS 查询键与资源记录键不一致导致的缓存失配问题**。

## 问题现象

以查询 `www.baidu.com A` 为例，上游服务器可能返回如下链式应答：

```text
www.baidu.com CNAME www.a.shifen.com
www.a.shifen.com A 220.181.111.232
www.a.shifen.com A 220.181.111.1
```

客户端发出的原始问题是：

```text
(www.baidu.com, A, IN)
```

但是响应中的资源记录并不都是：

```text
(www.baidu.com, A, IN)
```

而是分成了：

- `(www.baidu.com, CNAME, IN)`
- `(www.a.shifen.com, A, IN)`

这样就导致后续再次查询 `www.baidu.com A` 时，无法直接通过问题三元组命中之前缓存下来的 RR。也就是说，客户端真正使用的查询键与缓存写入时使用的资源记录键发生了偏离。

## 问题根因

问题的本质在于：**旧版缓存的存储粒度和索引语义不正确。**

旧实现的思路是：

- 把上游响应中的每一条 `RR` 单独缓存
- 缓存 key 使用该条 `RR` 自身的 `(name, type, class)`

这种方式在“响应结果中的 RR 与原始查询问题完全一致”时可以工作，例如：

```text
example.com A -> example.com A 1.2.3.4
```

但在存在 `CNAME` 链式跳转时就会失效，因为：

- 查询问题是：`(www.baidu.com, A, IN)`
- 被缓存的记录却是：
  - `(www.baidu.com, CNAME, IN)`
  - `(www.a.shifen.com, A, IN)`

也就是说，**缓存 key 来自 RR，而查询 key 来自 Question，两者在 `CNAME` 场景下并不一致**，从而导致缓存 miss。

## 解决思路

我们最终采用的解决思路是：

**不再按单条 RR 作为缓存单位，而是按“问题 -> 完整响应结果”进行缓存。**

也就是说，缓存的 key 固定为原始问题三元组：

```text
(qname, qtype, qclass)
```

缓存的 value 则不再是单条 RR，而是一整个响应结果集。这样缓存的索引语义与 DNS 查询语义保持一致，不再依赖响应中每条 RR 自身的 `name/type/class`。

## 具体实现方案

为适配项目当前的数据结构，我们引入了 `CacheValue` 作为缓存值：

```c
typedef struct {
    uint16_t answer_RRs;
    uint16_t authority_RRs;
    uint16_t additional_RRs;
    Vector* rrs;
} CacheValue;
```

其中：

- `rrs` 保存完整 RR 列表
- `answer_RRs / authority_RRs / additional_RRs` 记录三个段的条目数量

这样一来，缓存项的逻辑就从原来的：

```text
RR -> RR
```

变成了：

```text
Question -> CacheValue
```

也就是：

```text
(www.baidu.com, A, IN)
    -> [CNAME, A, A]
```

当同样的查询再次到来时，系统只需要按问题三元组查缓存，就能直接命中这一整组结果，而不需要再去分析其中的 `CNAME` 跳转关系。

## 方案优点

该方案的优点主要有以下几点：

1. 直接解决了 `CNAME` 链式响应下由于“查询键 / 资源记录键不一致”造成的缓存失配问题。
2. 缓存语义更加符合 DNS 查询本身，即“一个问题对应一个完整结果集”。
3. 缓存层职责更清晰，不需要在查询阶段额外进行多次查找或递归拼装。
4. 该模型同时适用于动态缓存和预缓存，便于统一实现。

## 总结

本问题说明，在 DNS 缓存设计中，缓存单位与缓存键的设计都非常关键。

如果仅仅按单条 `RR` 缓存，会在 `CNAME` 等链式响应场景下出现“查询键与资源记录键不一致”的问题；而将缓存粒度提升为“问题 -> 完整响应结果”，则能够更准确地反映 DNS 查询语义，也使缓存系统更加稳定和合理。

因此，本项目最终将缓存模型调整为：

```text
Question -> CacheValue
```

这也是解决“DNS 查询键与资源记录键不一致导致的缓存失配问题”的核心方案。
