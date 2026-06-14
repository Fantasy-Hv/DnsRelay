package main

import (
	"flag"
	"fmt"
	"net"
	"os"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

// ─────────────────────── DNS 协议 ───────────────────────

func buildQuery(domain string, qtype uint16) []byte {
	// DNS header (12 bytes): ID=0, Flags=RD, QDCOUNT=1
	header := [12]byte{0, 0, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
	buf := make([]byte, 0, 512)
	buf = append(buf, header[:]...)

	// QNAME: encode domain as length-prefixed labels
	for _, label := range strings.Split(domain, ".") {
		buf = append(buf, byte(len(label)))
		buf = append(buf, label...)
	}
	buf = append(buf, 0x00) // terminating zero-length label

	// QTYPE + QCLASS (IN=1)
	buf = append(buf, byte(qtype>>8), byte(qtype&0xFF))
	buf = append(buf, 0x00, 0x01)

	return buf
}

// ─────────────────────── 默认测试域名 ───────────────────────

type query struct {
	domain string
	qtype  uint16
}

var defaultQueries = []query{
	{"www.baidu.com", 1},
	{"www.bilibili.com", 1},
	{"localhost.example.com", 28},
	{"www.zhihu.com", 1},
	{"www.taobao.com", 1},
	{"www.jd.com", 1},
	{"www.weibo.com", 1},
	{"www.163.com", 1},
	{"www.sohu.com", 1},
	{"www.github.com", 1},
	{"qq.com", 1},
}

func repeatQueries(n int) []query {
	result := make([]query, n)
	for i := range result {
		result[i] = defaultQueries[i%len(defaultQueries)]
	}
	return result
}

// ─────────────────────── 数字格式化 ───────────────────────

func comma(n int) string {
	s := fmt.Sprintf("%d", n)
	if n < 1000 {
		return s
	}
	var b strings.Builder
	b.Grow(len(s) + len(s)/3)
	for i, c := range s {
		if i > 0 && (len(s)-i)%3 == 0 {
			b.WriteByte(',')
		}
		b.WriteRune(c)
	}
	return b.String()
}

// ─────────────────────── 测试引擎 ───────────────────────

func run(queries []query, workers int, timeout time.Duration, server string, port int) {
	total := len(queries)
	fmt.Printf("\n============================================================\n")
	fmt.Printf("  并发吞吐测试\n")
	fmt.Printf("  查询数: %s | 线程: %d | 超时: %.1fs | 目标: %s:%d\n",
		comma(total), workers, timeout.Seconds(), server, port)
	fmt.Printf("============================================================\n")

	addr := fmt.Sprintf("%s:%d", server, port)

	// Per-worker latency slices — avoids lock contention on hot path
	workerLatencies := make([][]float64, workers)
	// Pre-allocate per-worker capacity
	perWorker := (total + workers - 1) / workers
	for i := range workerLatencies {
		workerLatencies[i] = make([]float64, 0, perWorker)
	}

	var done atomic.Int64
	var errors atomic.Int64

	startTime := time.Now()

	// Progress monitor
	stopMonitor := make(chan struct{})
	go func() {
		ticker := time.NewTicker(500 * time.Millisecond)
		defer ticker.Stop()
		for {
			select {
			case <-stopMonitor:
				return
			case <-ticker.C:
				cur := done.Load()
				if cur >= int64(total) {
					return
				}
				elapsed := time.Since(startTime).Seconds()
				if elapsed < 0.001 {
					elapsed = 0.001
				}
				qps := float64(cur) / elapsed
				fmt.Printf("  [%6d/%d] QPS≈%.0f\n", cur, total, qps)
			}
		}
	}()

	// Chunk & dispatch — same logic as Python version
	chunkSize := total / workers
	if chunkSize < 1 {
		chunkSize = 1
	}

	var wg sync.WaitGroup
	workerID := 0
	for i := 0; i < total; i += chunkSize {
		end := i + chunkSize
		if end > total {
			end = total
		}
		wg.Add(1)
		wid := workerID
		tasks := queries[i:end]
		go func() {
			defer wg.Done()

			conn, err := net.DialTimeout("udp", addr, timeout)
			if err != nil {
				// Can't even create socket — count all as errors
				errors.Add(int64(len(tasks)))
				done.Add(int64(len(tasks)))
				return
			}
			defer conn.Close()

			lats := workerLatencies[wid]
			respBuf := make([]byte, 4096)

			for _, q := range tasks {
				raw := buildQuery(q.domain, q.qtype)

				conn.SetDeadline(time.Now().Add(timeout))
				t0 := time.Now()
				_, err := conn.Write(raw)
				if err != nil {
					errors.Add(1)
					done.Add(1)
					continue
				}

				_, err = conn.Read(respBuf)
				dt := time.Since(t0).Seconds()

				if err != nil {
					errors.Add(1)
				} else {
					lats = append(lats, dt)
				}
				done.Add(1)
			}
			workerLatencies[wid] = lats
		}()
		workerID++
	}

	wg.Wait()
	close(stopMonitor)

	elapsed := time.Since(startTime).Seconds()
	qps := float64(total) / elapsed
	if elapsed <= 0 {
		qps = 0
	}

	// Merge all latencies
	var allLatencies []float64
	for _, wl := range workerLatencies {
		allLatencies = append(allLatencies, wl...)
	}

	// ── Report ──
	fmt.Println()
	fmt.Printf("  总耗时:     %.3fs\n", elapsed)
	fmt.Printf("  查询数:     %d\n", total)
	fmt.Printf("  错误:       %d\n", errors.Load())
	fmt.Printf("  QPS:        %.1f\n", qps)
	fmt.Printf("  ────────────────────────────\n")
	if len(allLatencies) > 0 {
		sort.Float64s(allLatencies)
		n := len(allLatencies)
		sum := 0.0
		for _, l := range allLatencies {
			sum += l
		}
		avg := sum / float64(n)
		fmt.Printf("  平均:  %8.2f ms\n", avg*1000)
		fmt.Printf("  最小:  %8.2f ms\n", allLatencies[0]*1000)
		fmt.Printf("  最大:  %8.2f ms\n", allLatencies[n-1]*1000)
		fmt.Printf("  P50:   %8.2f ms\n", allLatencies[n/2]*1000)
		fmt.Printf("  P95:   %8.2f ms\n", allLatencies[int(float64(n)*0.95)]*1000)
		fmt.Printf("  P99:   %8.2f ms\n", allLatencies[int(float64(n)*0.99)]*1000)
	}
	fmt.Println()
}

// ─────────────────────── 入口 ───────────────────────

func main() {
	// Short + long flag aliases — same interface as Python benchmark
	server := flag.String("s", "127.0.0.1", "DNS 服务器地址")
	flag.StringVar(server, "server", "127.0.0.1", "DNS 服务器地址")

	port := flag.Int("p", 53, "DNS 服务器端口")
	flag.IntVar(port, "port", 53, "DNS 服务器端口")

	num := flag.Int("n", 5000, "查询总数")
	flag.IntVar(num, "num", 5000, "查询总数")

	workers := flag.Int("w", 10, "并发线程数")
	flag.IntVar(workers, "workers", 10, "并发线程数")

	timeoutSec := flag.Float64("t", 2.0, "超时秒数")
	flag.Float64Var(timeoutSec, "timeout", 2.0, "超时秒数")

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "用法: %s [选项]\n\n选项:\n", os.Args[0])
		flag.PrintDefaults()
	}

	flag.Parse()

	queries := repeatQueries(*num)
	run(queries, *workers, time.Duration(*timeoutSec*float64(time.Second)), *server, *port)
}
