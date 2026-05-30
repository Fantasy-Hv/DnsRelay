# DnsRelay — DNS 中继服务器

基于 RFC 1035 实现的 DNS 中继服务器。运行在本地 53 端口，拦截客户端 DNS 查询，根据本地 IP-域名映射表和缓存执行**三级查询响应**，本地无法应答时转发至上游 DNS 服务器。

## 核心功能

| 匹配结果                | 处理方式             | 功能定位      |
| ------------------- | ---------------- | --------- |
| 表中命中，IP 为 `0.0.0.0` | 返回"域名不存在"        | 不良网站拦截    |
| 表中命中，IP 为普通地址       | 直接返回该 IP         | 本地 DNS 服务 |
| 表中未命中，RD=1          | 转发上游 DNS，结果缓存并返回 | 中继功能      |

- **多客户端并发**：`select()` 事件驱动，单 socket 支持并发查询
- **DNS 缓存**：HashMap + LRU 双向链表，TTL 自动过期
- **超时重试**：可配置超时时间和最大重试次数
- **负载均衡**：上游服务器轮询调度

## 快速开始

### 环境要求

- Linux (POSIX)，C23 标准
- CMake 3.5+
- GCC / Clang

### 编译

```bash
cd scripts && bash build.sh
```

二进制文件输出到 `../bin/DnsRelay`。

### 运行

```bash
cd bin
./DnsRelay -d 10.3.9.6
```

### 命令行参数

```
dnsrelay [-d | -dd] [-c <配置文件路径>] [上游DNS服务器IP ...]
```

| 参数          | 说明                            |
| ----------- | ----------------------------- |
| `-d`        | 日志级别：DEBUG                    |
| `-dd`       | 日志级别：TRACE                    |
| `-c <path>` | 指定配置文件路径（默认 `./dnsrelay.ini`） |
| 上游ip        | 上游 DNS 服务器 IP，多个空格分隔          |

### 本地测试

将系统 DNS 设为 `127.0.0.1`，启动 DnsRelay 后使用 `nslookup` 或 `ping` 验证:

```bash
nslookup www.baidu.com 127.0.0.1
```

## 配置文件

### `dnsrelay.ini` — 服务器配置

```ini
[server]
 # 上游DNS服务器(逗号分隔)
server_upstream=10.3.9.6,10.3.9.4 
 # 请求超时(秒)
dns_packet_timeout=1               
# 最大重试次数
max_retry_time=1                    

[dns]
 # IP-域名映射表路径
iptable=./dnsrelay.txt             

[log]
# TRACE|DEBUG|INFO|WARN|ERROR
log_level=TRACE                     
# 可将各级别日志定向到文件
TRACE=./debug.txt                   
DEBUG=./debug.txt
```

### `dnsrelay.txt` — IP-域名映射表

```
# 拦截 (返回域名不存在)
www.bad-site.com=0.0.0.0

# 静态映射 (IPv4)，一个域名可对应多个 IP
www.bilibili.com=121.194.11.72
www.bilibili.com=121.194.11.73
www.bilibili.com=121.194.11.74
www.bilibili.com=121.194.11.75

# 静态映射 (IPv6)
localhost.example.com=::1
```

## 架构

```
main.c            — 入口：解析参数 → 初始化各模块 → 启动服务
server/           — 服务层：事件循环、会话管理、后台守护线程
dns/              — 核心层：协议解析、缓存、ID 分配
infra/            — 基础设施层：容器、配置、日志、Socket、异常追踪
```

详见  [docs/architect.md](docs/architect.md)。

## 运行测试

```bash
cd scripts && bash test_unit.sh
```

单独运行某个测试：

```bash
cd cmake-build-debug && ./test_cache
```

## 技术栈

- **语言**：C (C23)
- **构建**：CMake 3.5+
- **网络**：UDP Socket（IPv6 双栈，单 socket 收发）
- **并发**：`select()` 事件驱动 + 后台守护线程 (`<threads.h>`)
- **协议**：RFC 1035 (DNS)
