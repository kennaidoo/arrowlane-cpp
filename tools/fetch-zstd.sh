#!/bin/sh
# Fetch the zstd sources used by the ipc-zstd lane. Idempotent.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=1.5.7
SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3

if [ -f "$ROOT/vendor/zstd/lib/zstd.h" ]; then
    echo "vendor/zstd already present"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
curl -fsSL -o "$TMP/zstd.tar.gz" \
    "https://github.com/facebook/zstd/releases/download/v$VERSION/zstd-$VERSION.tar.gz"
echo "$SHA256  $TMP/zstd.tar.gz" | sha256sum -c -
tar -xzf "$TMP/zstd.tar.gz" -C "$TMP" "zstd-$VERSION/lib" "zstd-$VERSION/build"
mkdir -p "$ROOT/vendor/zstd"
mv "$TMP/zstd-$VERSION/lib" "$ROOT/vendor/zstd/lib"
mv "$TMP/zstd-$VERSION/build" "$ROOT/vendor/zstd/build"
