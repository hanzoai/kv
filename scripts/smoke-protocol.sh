#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

find_bin() {
  local override="$1"
  shift
  if [[ -n "$override" ]]; then
    echo "$override"
    return 0
  fi
  local candidate
  for candidate in "$@"; do
    if [[ -x "$ROOT_DIR/$candidate" ]]; then
      echo "$ROOT_DIR/$candidate"
      return 0
    fi
  done
  return 1
}

SERVER_BIN="$(find_bin "${KV_SERVER_BIN:-}" src/kv-server src/valkey-server)" || {
  echo "error: could not find executable kv-server/valkey-server under $ROOT_DIR/src (build first with 'make -j')" >&2
  exit 1
}
CLI_BIN="$(find_bin "${KV_CLI_BIN:-}" src/kv-cli src/valkey-cli)" || {
  echo "error: could not find executable kv-cli/valkey-cli under $ROOT_DIR/src" >&2
  exit 1
}
BENCH_BIN="$(find_bin "${KV_BENCH_BIN:-}" src/kv-benchmark src/valkey-benchmark)" || {
  echo "error: could not find executable kv-benchmark/valkey-benchmark under $ROOT_DIR/src" >&2
  exit 1
}

RUN_BENCH=1
for arg in "$@"; do
  case "$arg" in
    --no-bench) RUN_BENCH=0 ;;
    *)
      echo "usage: $0 [--no-bench]" >&2
      exit 2
      ;;
  esac
done

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/hanzo-kv-smoke.XXXXXX")"
SERVER_PID=""
SUB_PID=""

cleanup() {
  local rc=$?
  if [[ -n "$SUB_PID" ]]; then
    kill "$SUB_PID" 2>/dev/null || true
    wait "$SUB_PID" 2>/dev/null || true
  fi
  if [[ -n "$SERVER_PID" ]]; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -rf "$TMP_DIR"
  exit "$rc"
}
trap cleanup EXIT INT TERM

pick_free_port() {
  python3 - <<'PY'
import socket
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind(("127.0.0.1", 0))
    print(s.getsockname()[1])
PY
}

PORT="$(pick_free_port)"
SERVER_LOG="$TMP_DIR/kv-server.log"

"$SERVER_BIN" \
  --bind 127.0.0.1 \
  --port "$PORT" \
  --dir "$TMP_DIR" \
  --save "" \
  --appendonly no \
  --logfile "$SERVER_LOG" \
  --daemonize no \
  >/dev/null 2>&1 &
SERVER_PID=$!

kv() {
  "$CLI_BIN" -h 127.0.0.1 -p "$PORT" --raw "$@"
}

ready=0
for _ in $(seq 1 50); do
  if [[ "$(kv PING 2>/dev/null || true)" == "PONG" ]]; then
    ready=1
    break
  fi
  sleep 0.1
done
if [[ "$ready" -ne 1 ]]; then
  echo "error: $SERVER_BIN failed to become ready on 127.0.0.1:$PORT" >&2
  cat "$SERVER_LOG" >&2 || true
  exit 1
fi

assert_eq() {
  local label="$1"
  local actual="$2"
  local expected="$3"
  if [[ "$actual" != "$expected" ]]; then
    echo "FAIL [$label]: expected '$expected', got '$actual'" >&2
    exit 1
  fi
  echo "PASS [$label]: $actual"
}

echo "=== Hanzo KV Protocol Smoke Matrix (127.0.0.1:$PORT) ==="
assert_eq "PING" "$(kv PING)" "PONG"

# Strings: SET / GET / INCR
assert_eq "SET" "$(kv SET smoke:str hello-hanzo)" "OK"
assert_eq "GET" "$(kv GET smoke:str)" "hello-hanzo"
assert_eq "SET counter" "$(kv SET smoke:cnt 41)" "OK"
assert_eq "INCR counter" "$(kv INCR smoke:cnt)" "42"

# Hashes: HSET / HGET / HGETALL
assert_eq "HSET" "$(kv HSET smoke:hash engine kv lang c)" "2"
assert_eq "HGET" "$(kv HGET smoke:hash engine)" "kv"
HGETALL_OUT="$(kv HGETALL smoke:hash | tr '\n' ' ' | sed 's/[[:space:]]*$//')"
assert_eq "HGETALL" "$HGETALL_OUT" "engine kv lang c"

# Lists: LPUSH / RPUSH / LLEN / LRANGE
assert_eq "LPUSH" "$(kv LPUSH smoke:list b a)" "2"
assert_eq "RPUSH" "$(kv RPUSH smoke:list c)" "3"
assert_eq "LLEN" "$(kv LLEN smoke:list)" "3"
LRANGE_OUT="$(kv LRANGE smoke:list 0 -1 | tr '\n' ' ' | sed 's/[[:space:]]*$//')"
assert_eq "LRANGE" "$LRANGE_OUT" "a b c"

# Expiry: EXPIRE / TTL / PEXPIRE
assert_eq "SET ttlkey" "$(kv SET smoke:ttl temp-val)" "OK"
assert_eq "EXPIRE" "$(kv EXPIRE smoke:ttl 10)" "1"
TTL_VAL="$(kv TTL smoke:ttl)"
if [[ "$TTL_VAL" -le 0 || "$TTL_VAL" -gt 10 ]]; then
  echo "FAIL [TTL]: expected 1..10, got '$TTL_VAL'" >&2
  exit 1
fi
echo "PASS [TTL]: ${TTL_VAL}s"
assert_eq "PEXPIRE" "$(kv PEXPIRE smoke:ttl 150)" "1"
sleep 0.25
EXPIRED_VAL="$(kv GET smoke:ttl)"
assert_eq "EXPIRE elapsed GET" "$EXPIRED_VAL" ""

# Pub/Sub: SUBSCRIBE + PUBLISH
PUBSUB_OUT="$TMP_DIR/pubsub.out"
"$CLI_BIN" -h 127.0.0.1 -p "$PORT" --raw SUBSCRIBE smoke:chan >"$PUBSUB_OUT" 2>/dev/null &
SUB_PID=$!
sleep 0.2
PUB_CNT="$(kv PUBLISH smoke:chan "hello-pubsub")"
if [[ "$PUB_CNT" -lt 1 ]]; then
  echo "FAIL [PUBLISH]: expected subscriber count >= 1, got '$PUB_CNT'" >&2
  exit 1
fi
echo "PASS [PUBLISH]: subscribers=$PUB_CNT"
sleep 0.15
kill "$SUB_PID" 2>/dev/null || true
wait "$SUB_PID" 2>/dev/null || true
SUB_PID=""
if ! grep -q "hello-pubsub" "$PUBSUB_OUT"; then
  echo "FAIL [SUBSCRIBE]: did not receive 'hello-pubsub' in:" >&2
  cat "$PUBSUB_OUT" >&2
  exit 1
fi
echo "PASS [SUBSCRIBE]: received 'hello-pubsub'"

if [[ "$RUN_BENCH" -eq 1 ]]; then
  BENCH_REQUESTS="${BENCH_REQUESTS:-50000}"
  BENCH_CLIENTS="${BENCH_CLIENTS:-50}"
  echo ""
  echo "=== Baseline Throughput Benchmark ($BENCH_REQUESTS requests, $BENCH_CLIENTS clients) ==="
  "$BENCH_BIN" -h 127.0.0.1 -p "$PORT" \
    -t ping_inline,set,get,incr,lpush,rpush,lpop,hset \
    -n "$BENCH_REQUESTS" -c "$BENCH_CLIENTS" --csv
fi
