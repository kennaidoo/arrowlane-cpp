#!/bin/sh
# Runs the whole suite inside the container. /src is read-only; the build and
# the fetched zstd sources live under /tmp.
set -eu

BUILD=/tmp/build
VENDOR=/tmp/vendor

if [ ! -d "$VENDOR/zstd/lib" ]; then
    if [ -d /src/vendor/zstd/lib ]; then
        cp -a /src/vendor "$VENDOR"
    else
        mkdir -p "$VENDOR"
        cp -a /src/. /tmp/src
        cd /tmp/src && sh tools/fetch-zstd.sh
        cp -a /tmp/src/vendor "$VENDOR"
        cd /src
    fi
fi

cmake -S /src -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DAL_BUILD_TESTS=ON \
    -DAL_BUILD_ZSTD=ON \
    -DCMAKE_PREFIX_PATH=/tmp \
    -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"

cmake --build "$BUILD"

ctest --test-dir "$BUILD" --output-on-failure

echo "== fuzz_test =="
"$BUILD/fuzz_test"

echo "== check_wire =="
PYTHONPATH=/arrowlane/src python3 /src/interop/check_wire.py \
    --exe "$BUILD/arrowlane-cpp" \
    --rows "${ROWS:-200}" \
    --lanes json,ipc,ipc-zstd

if [ -f /src/interop/fuzz_wire.py ]; then
    echo "== fuzz_wire =="
    PYTHONPATH=/arrowlane/src python3 /src/interop/fuzz_wire.py \
        --exe "$BUILD/arrowlane-cpp" --cases "${CASES:-50}"
fi
