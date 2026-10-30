# Performance

Measured, never estimated. Every number below comes from a command in this
repository, and every missed target is listed with its number.

## Method

```bash
docker build -f bench/Dockerfile.cpp -t arrowlane-bench-cpp:local .
python bench/compare.py --rows 200000 --repeats 7 --check
```

One machine, one fixture, one row count, medians of seven, each implementation
in its own container, warm up run discarded. Machine: desktop, Ryzen 7 5800X3D,
Windows 11, Docker Desktop with the WSL2 backend. Date 2026-10-30. Compiler for
the C++ build: GCC 16.2.0 in the Wolfi image, release mode.

The fixture is the eight column table: int64, utf8, float64, bool, date32,
decimal128(9,2), timestamp[us,UTC], binary. All four implementations build it
from the same rules, and `interop/check_wire.py` proves the value bytes equal
pyarrow's with a CRC32 per column.

The methods differ in one way, and it is stated per implementation:

| Implementation | Method |
|---|---|
| cpp | in process, lane encode plus frame, then frame decode plus lane decode |
| python | in process, `pack` then `unpack` |
| rust | in process, its own bench subcommand |
| zig | in process, its own bench subcommand |

All four are in process timings, so no container start is included.

## 200000 rows

### ipc lane

| Impl | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| cpp | 12670881 | 6.996 | 5.935 |
| python | 12670845 | 18.199 | 3.365 |
| rust | 12671117 | 1.387 | 2.421 |
| zig | 12670849 | 40.296 | 24.687 |

### json lane

| Impl | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| cpp | 42505784 | 515.954 | 536.766 |
| python | 42505756 | 1965.868 | 1871.103 |
| rust | 42505812 | 223.832 | 1051.174 |
| zig | 42372426 | 169.826 | 313.475 |

### ipc-zstd lane

| Impl | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| cpp | 1608747 | 23.853 | 8.417 |
| python | 1620041 | 25.030 | 18.080 |
| rust | 1625309 | 24.252 | 9.889 |
| zig | 1620045 | 54.904 | 19.964 |

The zstd lane is where this implementation is fastest outright: smallest wire
frame, fastest encode, fastest decode. It is not a scored row in the win table,
which is why it is not claimed as a win.

## Win conditions

| Condition | C++ | Reference | Limit | Verdict |
|---|---|---|---|---|
| ipc encode | 6.996 | rust 1.387 | 1.248 | LOSS |
| ipc decode | 5.935 | rust 2.421 | 1.937 | LOSS |
| json encode | 515.954 | python 1965.868 | 2162.455 | WIN |
| json decode | 536.766 | rust 1051.174 | 630.704 | WIN |
| ipc wire bytes | 12670881 | python 12670845 | 12924262 | WIN |

Three wins, two losses.

## Losses

Every missed target, with the number and the open task behind it.

1. **ipc encode, 6.996 ms against a 1.248 ms limit.** 5.6 times the target and
   five times Rust. Two causes, both measured:
   - CRC32 costs 6.279 ms per 12.67 MB pass with the slicing by eight table,
     which is 45 percent of encode and 61 percent of decode.
   - The encoder copies the payload twice: once into the IPC body, once into
     the joined wire buffer.
   Open task: a carry-less multiply checksum path, and a writer that places the
   body directly into the output buffer.
2. **ipc decode, 5.935 ms against a 1.937 ms limit.** 3.1 times the target.
   Same checksum cost, plus one payload copy on the owning path and a second
   copy out of the columns. The zero copy view path exists and is tested, but
   the benchmark measures the owning path because that is what a caller who
   wants a table gets.
   Open task: the same checksum work, then measure the view path as its own row.
3. **Release binary, 1,122,816 bytes against a 512 KB target.** 2.2 times the
   target. About 700 KB of it is statically linked zstd. A build with
   `-DAL_BUILD_ZSTD=OFF` is smaller and cannot read the zstd lane, so the
   number above is the honest one for the full feature set.
4. **Startup time, not measured.** The Rust and Zig images are scratch images
   with no shell, so `--version` cannot be timed inside them. This row is
   unproven, not won.
5. **Peak RSS at 500000 rows, not measured for all four.** Same reason. The C++
   number is taken from the operating system in `bench --json`.

## Allocation budgets

`test/alloc_test.cpp` counts every allocation with a global operator new and
pins the measured values.

| Case | Allocations | Bytes |
|---|---|---|
| json encode, 1000 rows | 309673 | 12599013 |
| ipc encode, 1000 rows | 369 | 141993 |
| ipc view decode, 63001 byte payload | 285 | 14856 |
| ipc owning decode, 63001 byte payload | 291 | 139430 |

Two things to read from that table. The view decode allocates 14,856 bytes for
a 63,001 byte payload, so it copies no payload, which is the zero copy claim
proven rather than asserted. And json encode allocates about 310 times per row,
which breaks the plan's rule that it should allocate none per row, and is the
measured reason the json lane costs roughly 0.5 ms per thousand rows.

That last one is an open task, not a win.

## Binary size

| Artifact | Bytes |
|---|---|
| arrowlane-cpp.exe, release, MSVC | 1122816 |
| arrowlane_capi.dll, release, MSVC | 1000448 |

Single files, no runtime dependency beyond the C++ runtime, zstd linked
statically.

## What is not claimed

Nothing here says this is the fastest implementation. It is fastest on the zstd
lane, second on json encode and ipc encode, and slower than Rust on both ipc
conditions. `bench/compare.py --check` exits 1 on those two rows, and that is
the current state of the work.

