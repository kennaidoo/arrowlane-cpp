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

echo "== check_dictionary =="
# Two directions: the C++ test writes a dictionary stream for pyarrow to read,
# and it reads the stream pyarrow wrote. The pyarrow fixture is committed, so the
# generator is not run here: the source mount is read only, and regenerating it
# is a maintainer step (interop/make_dict_fixture.py).
"$BUILD/dictionary_test"
"${PYTHON:-python3}" /src/interop/check_dictionary.py --path "$BUILD/dict_cpp.arrows"

echo "== check_bytes =="
# One table, both implementations, compared. The json payload must be identical to
# the byte; the ipc data must match buffer for buffer, while the flatbuffer layout
# of the metadata is allowed to differ.
ARROWLANE_SRC=/arrowlane/src ARROWLANE_BENCH=/arrowlane/bench/containers \
  "${PYTHON:-python3}" /src/interop/check_bytes.py --exe "$BUILD/arrowlane-cpp" \
  --rows 200000

echo "== check_cabi =="
# The library is built with ASan, so a plain Python process cannot dlopen it.
# Preload the runtime; leak detection stays off because Python is not
# instrumented.
ASAN_RUNTIME="$("${CC:-gcc}" -print-file-name=libasan.so 2>/dev/null || true)"
if [ -n "$ASAN_RUNTIME" ] && [ "$ASAN_RUNTIME" != "libasan.so" ]; then
    LD_PRELOAD="$ASAN_RUNTIME" ASAN_OPTIONS=detect_leaks=0     ARROWLANE_SRC=/arrowlane/src "${PYTHON:-python3}" /src/interop/check_cabi.py         --dll "$BUILD/libarrowlane_capi.so"
else
    ARROWLANE_SRC=/arrowlane/src "${PYTHON:-python3}" /src/interop/check_cabi.py         --dll "$BUILD/libarrowlane_capi.so"
fi

echo "== check_schema =="
ARROWLANE_SRC=/arrowlane/src "${PYTHON:-python3}" /src/interop/check_schema.py     --exe "$BUILD/arrowlane-cpp"
