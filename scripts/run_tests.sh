#!/usr/bin/env bash
# Build and run all tests.
set -euo pipefail

BUILD_DIR="${1:-build}"
BUILD_TYPE="${2:-Debug}"

echo "=== Building tests ($BUILD_TYPE) ==="
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" --parallel

echo ""
echo "=== Running unit tests ==="
cd "$BUILD_DIR"
ctest --output-on-failure --parallel "$(nproc)"
