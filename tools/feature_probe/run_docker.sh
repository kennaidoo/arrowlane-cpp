#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
if command -v cygpath >/dev/null 2>&1; then
  mount="$(cygpath -m "$root")"
else
  mount="$root"
fi
docker run --rm -v "${mount}:/src:ro" -w /src rust:1.99.1-bookworm bash -c '
  g++ --version | head -1
  for f in tools/feature_probe/*.cpp; do
    if g++ -std=c++23 -fsyntax-only "$f" >/dev/null 2>&1; then
      echo "PASS $f"
    else
      echo "FAIL $f"
    fi
  done'
