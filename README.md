# arrowlane-cpp

A C++23 implementation of the arrowlane wire frame, the Arrow schema
flatbuffer codec, and four transport lanes. It reads and writes frame format
version 1 and interoperates with the Python package `arrowlane` 0.3.1 in both
directions.

There is no Arrow C++ dependency and no Python at runtime. The only third
party code is zstd, fetched by script for the `ipc-zstd` lane.

## Status

Version 0.1.0. Nothing is published yet, so the version says so. The wire format
is frozen at format version 1 and is checked against pyarrow on every run; the
package version stays below 1.0.0 until there is a published release.

What works today:

| Area | State |
|---|---|
| frame encode and decode, CRC32 | complete |
| Arrow schema flatbuffer, read and write | complete |
| `json` lane (NDJSON) | complete |
| `ipc` lane (Arrow IPC stream) | complete, zero copy read path |
| `ipc-zstd` lane (zstd level 3) | complete |
| `shm` lane (POSIX shared memory) | complete on Linux, absent on Windows |
| lane selector from a measured cost model | complete, 388 parity cases |
| chunk streams with backpressure | complete |
| C ABI for foreign bindings | complete |
| benchmarks against Python, Rust and Zig | complete, see `docs/performance.md` |

Every claim above is covered by a test that runs on two compilers, and the
interop checks compare values, schema and digests against the reference in both
directions. The rows that are not proven are listed in `docs/performance.md`
under Losses, with their numbers.

## Build

Windows, from a shell with the MSVC build tools available:

```bat
scripts\build.cmd
scripts\gate.cmd
```

Linux or macOS, with GCC 12 or newer:

```bash
bash tools/fetch-zstd.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`scripts\build.cmd Release` writes to `build-release`. A debug build is about
thirty times slower, so never quote a benchmark from it. `describe` reports the
build type it was compiled with.

## Test

The suite runs in a container as well, which is the only place the POSIX lanes
are exercised:

```bat
docker build -f tools/Dockerfile.test -t arrowlane-cpp-test:wolfi .
tools\run-tests.cmd
```

That runs `ctest`, the fuzz harness, and six cross-language checks: wire
values, wire fuzzing, selector parity, chunk streams, the C ABI, and the schema
message.

## Lanes

| Id | Name | Media type | Notes |
|---|---|---|---|
| 1 | `json` | `application/x-ndjson` | readable, largest, allocates per row |
| 2 | `ipc` | `application/vnd.apache.arrow.stream` | zero copy read path |
| 3 | `ipc-zstd` | `application/vnd.arrowlane.zstd` | zstd, bounded decode |
| 4 | `shm` | `application/vnd.arrowlane.shm` | POSIX only, payload stays in a segment |

## Supported types

Null, Bool, Int8 to Int64, UInt8 to UInt64, Float16, Float32, Float64, Utf8,
LargeUtf8, Binary, LargeBinary, FixedSizeBinary, Decimal128, Decimal256,
Date32, Date64, Time32, Time64, Timestamp with second, millisecond, microsecond
or nanosecond units, and Duration.

## Not supported

Nested types (List, LargeList, FixedSizeList, Struct, Map, Union), dictionary
encoding, run end encoding, the Arrow view types, Float8, and interval types.
A frame that carries one of these is rejected with a clear error rather than
read incorrectly. Decimal256 is carried by the frame and the IPC lane but not
by the json lane.

## Benchmarks

The four way comparison runs each implementation in its own container against
the same fixture:

```bash
python bench/compare.py --rows 200000 --repeats 7 --rounds 3 --check
```

Every row reports a median with a range, and the verdict is taken on the ratio
per round, so a machine that changes speed mid-run cannot turn a tie into a win.
The current record, including the one row that is not settled, is in
`docs/performance.md`.

## Layout

```text
include/arrowlane/   public headers, including cabi.h
src/                 implementation, detail/ holds the internals
test/                one binary per area, plus the harness
interop/             cross-language checks against the Python reference
tools/               container image, zstd fetch, feature probes
docs/                decisions, feature matrix, agent docs
```

## License

Apache 2.0. See `LICENSE`.
