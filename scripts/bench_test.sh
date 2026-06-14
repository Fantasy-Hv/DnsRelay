#!/usr/bin/env bash
# ============================================================
# DnsRelay 客户端基准测试
# 用法:
#   bash bench_test.sh                           # 默认 127.0.0.1:53
#   SERVER=10.0.0.1 bash bench_test.sh            # 指定服务端 IP
#   SERVER=10.0.0.1 PORT=5353 bash bench_test.sh  # 指定 IP 和端口
#   QUERY_NUM=50000 WORKERS=20 bash bench_test.sh # 自定义压测参数
# ============================================================
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BENCH_DIR="$ROOT_DIR/bench"
BENCH_BIN="$BENCH_DIR/benchmark"

# ─── 参数，全部支持环境变量覆盖 ───
SERVER=${SERVER:-127.0.0.1}
PORT=${PORT:-53}
QUERY_NUM=${QUERY_NUM:-2000000}
WORKERS=${WORKERS:-200}
TIMEOUT=${TIMEOUT:-2.0}

# ─── 构建 Go benchmark（如需要） ───
NEED_BUILD=0
if [ ! -x "$BENCH_BIN" ]; then
  NEED_BUILD=1
elif [ "$BENCH_DIR/benchmark.go" -nt "$BENCH_BIN" ]; then
  NEED_BUILD=1
fi

if [ "$NEED_BUILD" -eq 1 ]; then
  echo "[INFO] 构建 benchmark..."
  (cd "$BENCH_DIR" && go build -o benchmark benchmark.go)
fi

# ─── 运行 ───
echo "目标: $SERVER:$PORT | 查询数: $QUERY_NUM | 线程: $WORKERS | 超时: ${TIMEOUT}s"

exec "$BENCH_BIN" \
  --server "$SERVER" \
  --port "$PORT" \
  --num "$QUERY_NUM" \
  --timeout "$TIMEOUT" \
  --workers "$WORKERS"
