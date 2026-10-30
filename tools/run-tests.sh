#!/bin/sh
# Runs the whole suite inside the container. /src is read-only; the build and
# the fetched zstd sources live under /tmp.
set -eu

BUILD=/tmp/build
VENDOR=/tmp/vendor

# AddressSanitizer intermittently dies at startup in containers when the kernel
# randomises the address space with high entropy (AddressSanitizer:DEADLYSIGNAL
# loop). Disable ASLR for the test processes only. Needs seccomp unconfined.
run_quiet_aslr() {
    if command -v setarch >/dev/null 2>&1 && setarch -R true 2>/dev/null; then
        setarch -R "$@"
    else
        "$@"
    fi
}

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

run_quiet_aslr ctest --test-dir "$BUILD" --output-on-failure

echo "== fuzz_test =="
run_quiet_aslr "$BUILD/fuzz_test"

echo "== check_wire =="
ARROWLANE_SRC=/arrowlane/src "${PYTHON:-python3}" /src/interop/check_wire.py \
    --exe "$BUILD/arrowlane-cpp" \
    --rows "${ROWS:-200}" \
    --lanes json,ipc,ipc-zstd

if [ -f /src/interop/fuzz_wire.py ]; then
    echo "== fuzz_wire =="
    ARROWLANE_SRC=/arrowlane/src "${PYTHON:-python3}" /src/interop/fuzz_wire.py \
        --exe "$BUILD/arrowlane-cpp" --cases "${CASES:-50}"
fi

echo "== check_select =="
ARROWLANE_SRC=/arrowlane/src "${PYTHON:-python3}" /src/interop/check_select.py \
    --exe "$BUILD/arrowlane-cpp"

echo "== check_chunks =="
ARROWLANE_SRC=/arrowlane/src "${PYTHON:-python3}" /src/interop/check_chunks.py     --exe "$BUILD/arrowlane-cpp"
