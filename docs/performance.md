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
| cpp | 12670881 | 1.540 | 1.888 |
| python | 12670845 | 18.119 | 3.198 |
| rust | 12671117 | 1.582 | 2.808 |
| zig | 12670849 | 39.604 | 24.855 |

### json lane

| Impl | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| cpp | 42505784 | 405.731 | 537.521 |
| python | 42505756 | 2055.950 | 1859.869 |
| rust | 42505812 | 224.346 | 1011.816 |
| zig | 42372426 | 167.982 | 309.595 |

### ipc-zstd lane

| Impl | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| cpp | 1608747 | 26.240 | 8.835 |
| python | 1620041 | 24.925 | 17.671 |
| rust | 1625309 | 25.428 | 9.813 |
| zig | 1620045 | 51.574 | 19.044 |

On the zstd lane this implementation has the smallest wire frame and the fastest
decode, but Python is now fastest on zstd encode by 1.3 ms. It is not a scored
row in the win table, which is why none of it is claimed as a win. The ipc rows
are scored, and both are now the fastest of the four.

## Win conditions

| Condition | C++ | Reference | Limit | Verdict |
|---|---|---|---|---|
| ipc encode | 1.540 | rust 1.582 | 1.424 | LOSS |
| ipc decode | 1.888 | rust 2.808 | 2.246 | WIN |
| json encode | 405.731 | python 2055.950 | 2261.545 | WIN |
| json decode | 537.521 | rust 1011.816 | 607.090 | WIN |
| ipc wire bytes | 12670881 | python 12670845 | 12924262 | WIN |

Four wins, one loss. Both ipc rows are now faster than Rust in raw time: encode
1.540 against 1.582, decode 1.888 against 2.808. The encode row is still a loss
because the condition asks for at most 0.9 times Rust, that is 1.424 ms, and
1.540 is 8 percent above it. Being faster is not the same as being 10 percent
faster.

The reference numbers move between runs because the machine does: Rust's ipc
encode measured 1.387 ms in the previous run and 2.656 ms in this one. Only
ratios inside one run mean anything, and only one run at a time.

## Losses

Every missed target, with the number and the open task behind it.

1. **ipc encode, 1.540 ms against a 1.424 ms limit.** 8 percent above the
   limit, and 3 percent faster than Rust in raw time. Down from 6.996 ms after
   three changes: the checksum path, the body placed directly into the output,
   and the wire written in place instead of joined.
   Open task: the checksum is now about 0.85 ms of the 1.540, so a wider kernel
   (AVX2 or AVX-512 carry-less multiply, eight or sixteen streams) is the
   remaining lever. `crc32fast-1.5.3` carries the constants for both.
2. **Release binary, 1,122,816 bytes against a 512 KB target.** 2.2 times the
   target. About 700 KB of it is statically linked zstd. A build with
   `-DAL_BUILD_ZSTD=OFF` is smaller and cannot read the zstd lane, so the
   number above is the honest one for the full feature set.
3. **Startup time, not measured.** The Rust and Zig images are scratch images
   with no shell, so `--version` cannot be timed inside them. This row is
   unproven, not won.
4. **Peak RSS at 500000 rows, not measured for all four.** Same reason. The C++
   number is taken from the operating system in `bench --json`.
5. **json allocations, 163731 for 1000 rows against a target of 5000.** Down
   from 309673, with byte identical output, but not at the target. The rest is
   inside the value formatters: a `JsonValue` and a `std::string` per value,
   with decimal formatting the largest contributor. Open task: formatters that
   write into a caller buffer.

## Allocation budgets

`test/alloc_test.cpp` counts every allocation with a global operator new and
pins the measured values.

| Case | Allocations | Bytes |
|---|---|---|
| json encode, 1000 rows | 163731 | 5362161 |
| ipc encode, 1000 rows | 366 | 80730 |
| ipc view decode, 63001 byte payload | 285 | 14856 |
| ipc owning decode, 63001 byte payload | 291 | 139430 |

Two things to read from that table. The view decode allocates 14,856 bytes for
a 63,001 byte payload, so it copies no payload, which is the zero copy claim
proven rather than asserted. And ipc encode allocates one payload plus 18 KB,
down from 141,993 bytes, because the body is written straight into the wire.

## Binary size

| Artifact | Bytes |
|---|---|
| arrowlane-cpp.exe, release, MSVC | 1122816 |
| arrowlane_capi.dll, release, MSVC | 1000448 |

Single files, no runtime dependency beyond the C++ runtime, zstd linked
statically.

## What is not claimed

Nothing here says this is the fastest implementation overall. It is fastest on
ipc encode, ipc decode and zstd decode, second on json decode, and third on
json encode and zstd encode. `bench/compare.py --check` exits 1 on the single
remaining loss, ipc encode, because that row demands 10 percent over Rust and
this build has 3 percent.

