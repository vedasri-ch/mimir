#!/usr/bin/env bash
# Run Mimir in-process benchmark and optionally redis-benchmark against
# the live server.
set -euo pipefail

BUILD_DIR="${1:-build}"

echo "=== Building in Release mode ==="
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$BUILD_DIR" --parallel

echo ""
echo "=== In-process benchmark ==="
"$BUILD_DIR/mimir_bench"

echo ""
echo "=== Network benchmark (redis-benchmark, requires running server) ==="
if command -v redis-benchmark &>/dev/null; then
  redis-benchmark -h 127.0.0.1 -p 6379 -c 50 -n 100000 -t set,get,incr,lpush,rpush,ping \
    --csv | column -t -s ','
else
  echo "redis-benchmark not found. Install redis-tools to run network benchmarks."
fi
