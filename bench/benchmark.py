#!/usr/bin/env python3
"""
DnsRelay 性能基准测试 — 并发吞吐测试
是否使用缓存由 dnsrelay.ini 的 [dnsresolver] usecache 控制
"""

import socket, struct, time, argparse, threading

# ─────────────────────── DNS 协议 ───────────────────────

def build_query(domain, qtype=1, qid=0):
    flags = 0x0100  # RD=1
    header = struct.pack('>HHHHHH', qid & 0xFFFF, flags, 1, 0, 0, 0)
    qname = b''
    for label in domain.encode('ascii').split(b'.'):
        qname += bytes([len(label)]) + label
    qname += b'\x00'
    qname += struct.pack('>HH', qtype, 1)
    return header + qname


def parse_response(data):
    if len(data) < 12:
        return None, None, None
    return struct.unpack('>HHHHHH', data[:12])[0], data[3] & 0xF, struct.unpack('>H', data[6:8])[0]


# ─────────────────────── 默认测试域名 ───────────────────────

DEFAULT_QUERIES = [
    ('www.baidu.com', 1),
    ('www.bilibili.com', 1),
    ('localhost.example.com', 28),
    ('www.zhihu.com', 1),
    ('www.taobao.com', 1),
    ('www.jd.com', 1),
    ('www.weibo.com', 1),
    ('www.163.com', 1),
    ('www.sohu.com', 1),
    ('www.github.com', 1),
    ('qq.com', 1),
]


def repeat(lst, n):
    return (lst * (n // len(lst) + 1))[:n]


# ─────────────────────── 测试引擎 ───────────────────────

def run(queries, workers=10, timeout=2.0, server='127.0.0.1', port=53):
    total = len(queries)
    print(f'\n{"="*60}')
    print(f'  并发吞吐测试')
    print(f'  查询数: {total:,} | 线程: {workers} | 超时: {timeout}s | 目标: {server}:{port}')
    print(f'{"="*60}')

    latencies = []
    errors = [0]
    done = [0]
    lock = threading.Lock()

    def worker(task_list):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        for domain, qtype in task_list:
            try:
                raw = build_query(domain, qtype)
                t0 = time.perf_counter()
                sock.sendto(raw, (server, port))
                data, _ = sock.recvfrom(4096)
                dt = time.perf_counter() - t0
                with lock:
                    latencies.append(dt)
            except:
                with lock:
                    errors[0] += 1
            with lock:
                done[0] += 1
        sock.close()

    chunk = max(1, total // workers)
    chunks = [queries[i:i+chunk] for i in range(0, total, chunk)]
    threads = []

    t0 = time.perf_counter()
    for c in chunks:
        t = threading.Thread(target=worker, args=(c,), daemon=True)
        t.start()
        threads.append(t)

    last = 0
    while any(t.is_alive() for t in threads):
        time.sleep(0.5)
        cur = done[0]
        if cur != last:
            qps = cur / max(time.perf_counter() - t0, 0.001)
            print(f'  [{cur:>6d}/{total}] QPS≈{qps:.0f}', flush=True)
            last = cur

    for t in threads:
        t.join()

    elapsed = time.perf_counter() - t0

    # 报告
    qps = total / elapsed if elapsed > 0 else 0
    print(f'')
    print(f'  总耗时:     {elapsed:.3f}s')
    print(f'  查询数:     {total}')
    print(f'  错误:       {errors[0]}')
    print(f'  QPS:        {qps:.1f}')
    print(f'  ────────────────────────────')
    if latencies:
        s = sorted(latencies)
        n = len(s)
        avg = sum(s) / n
        print(f'  平均:  {avg*1000:>8.2f} ms')
        print(f'  最小:  {min(s)*1000:>8.2f} ms')
        print(f'  最大:  {max(s)*1000:>8.2f} ms')
        print(f'  P50:   {s[n//2]*1000:>8.2f} ms')
        print(f'  P95:   {s[int(n*0.95)]*1000:>8.2f} ms')
        print(f'  P99:   {s[int(n*0.99)]*1000:>8.2f} ms')
    print()


# ─────────────────────── 入口 ───────────────────────

def main():
    p = argparse.ArgumentParser(description='DnsRelay 并发吞吐测试')
    p.add_argument('-s', '--server', default='127.0.0.1')
    p.add_argument('-p', '--port', type=int, default=53)
    p.add_argument('-n', '--num', type=int, default=5000, help='查询总数')
    p.add_argument('-w', '--workers', type=int, default=10, help='并发线程数')
    p.add_argument('-t', '--timeout', type=float, default=2.0, help='超时秒数')
    args = p.parse_args()

    queries = repeat(DEFAULT_QUERIES, args.num)
    run(queries, args.workers, args.timeout, args.server, args.port)


if __name__ == '__main__':
    main()
