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
VENV_DIR="$ROOT_DIR/.venv"
BENCH_SCRIPT="$ROOT_DIR/bench/benchmark.py"

# ─── 参数，全部支持环境变量覆盖 ───
SERVER=${SERVER:-127.0.0.1}
PORT=${PORT:-53}
QUERY_NUM=${QUERY_NUM:-1000000}
WORKERS=${WORKERS:-50}
TIMEOUT=${TIMEOUT:-2.0}

# ─── 检查 Python venv ───
if [ ! -f "$VENV_DIR/bin/python" ]; then
  echo "[ERROR] 虚拟环境不存在: $VENV_DIR"
  exit 1
fi
PYTHON="$VENV_DIR/bin/python"

"$PYTHON" -c "import dns.query" 2>/dev/null || {
  echo "[INFO] 安装 dnspython..."
  "$VENV_DIR/bin/pip" install -q dnspython
}

# ─── 运行 ───
echo "目标: $SERVER:$PORT | 查询数: $QUERY_NUM | 线程: $WORKERS | 超时: ${TIMEOUT}s"

"$PYTHON" "$BENCH_SCRIPT" \
  --server "$SERVER" \
  --port "$PORT" \
  --num "$QUERY_NUM" \
  --timeout "$TIMEOUT" \
  --workers "$WORKERS"
