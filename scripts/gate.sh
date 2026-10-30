#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$root" -B "$root/build-linux" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DAL_SANITIZE=ON
cmake --build "$root/build-linux"
ctest --test-dir "$root/build-linux" --output-on-failure
echo "RESULT: PASS"
