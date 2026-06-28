#!/usr/bin/env bash
# Stress test: starts the server, hammers it with concurrent clients,
# then verifies crash-recovery via WAL replay.
set -euo pipefail

BUILD_DIR="${1:-build}"
PORT=6399  # use a non-default port to avoid conflicts

start_server() {
  "$BUILD_DIR/mimir" config.yaml &
  SERVER_PID=$!
  sleep 0.5
  echo "[stress] Server started (pid=$SERVER_PID)"
}

stop_server() {
  kill "$SERVER_PID" 2>/dev/null || true
  wait "$SERVER_PID" 2>/dev/null || true
  echo "[stress] Server stopped"
}

send_commands() {
  local count=$1
  for i in $(seq 1 "$count"); do
    printf "SET stress_key_%d value_%d\r\n" "$i" "$i"
  done | nc -q1 127.0.0.1 6379 > /dev/null
}

echo "=== Building ==="
cmake --build "$BUILD_DIR" --parallel

# Phase 1: write data
echo "=== Phase 1: writing 5000 keys ==="
start_server
send_commands 5000
echo "[stress] Simulating crash (SIGKILL)..."
kill -9 "$SERVER_PID" 2>/dev/null || true
sleep 0.3

# Phase 2: restart and verify WAL replay
echo "=== Phase 2: restarting server, verifying WAL replay ==="
start_server
REPLY=$(printf "GET stress_key_1000\r\n" | nc -q1 127.0.0.1 6379)
if echo "$REPLY" | grep -q "value_1000"; then
  echo "[stress] PASS: WAL replay correct (key restored after crash)"
else
  echo "[stress] FAIL: key not found after replay (reply: $REPLY)"
  stop_server
  exit 1
fi
stop_server

# Phase 3: concurrent clients
echo "=== Phase 3: concurrent clients (50 parallel) ==="
start_server
for i in $(seq 1 50); do
  send_commands 200 &
done
wait
echo "[stress] All concurrent clients finished"
stop_server

echo ""
echo "=== Stress test PASSED ==="
