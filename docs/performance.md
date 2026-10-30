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
| cpp | 12670881 | 3.449 | 2.640 |
| python | 12670845 | 21.601 | 3.785 |
| rust | 12671117 | 2.656 | 3.490 |
| zig | 12670849 | 46.989 | 26.738 |

### json lane

| Impl | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| cpp | 42505784 | 418.034 | 594.791 |
| python | 42505756 | 2212.442 | 2076.506 |
| rust | 42505812 | 252.245 | 1182.569 |
| zig | 42372426 | 195.108 | 352.970 |

### ipc-zstd lane

| Impl | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| cpp | 1608747 | 28.455 | 10.144 |
| python | 1620041 | 31.133 | 26.165 |
| rust | 1625309 | 30.259 | 11.885 |
| zig | 1620045 | 65.474 | 22.717 |

The zstd lane is where this implementation is fastest outright: smallest wire
frame, fastest encode, fastest decode. It is not a scored row in the win table,
which is why it is not claimed as a win. The ipc decode row is a scored row and
it is now the fastest of the four.

## Win conditions

| Condition | C++ | Reference | Limit | Verdict |
|---|---|---|---|---|
| ipc encode | 3.449 | rust 2.656 | 2.390 | LOSS |
| ipc decode | 2.640 | rust 3.490 | 2.792 | WIN |
| json encode | 418.034 | python 2212.442 | 2433.686 | WIN |
| json decode | 594.791 | rust 1182.569 | 709.541 | WIN |
| ipc wire bytes | 12670881 | python 12670845 | 12924262 | WIN |

Four wins, one loss. ipc decode now beats Rust outright, and ipc encode is
1.3 times Rust instead of 5 times.

The reference numbers move between runs because the machine does: Rust's ipc
encode measured 1.387 ms in the previous run and 2.656 ms in this one. Only
ratios inside one run mean anything, and only one run at a time.

## Losses

Every missed target, with the number and the open task behind it.

1. **ipc encode, 3.449 ms against a 2.390 ms limit.** 1.44 times the limit and
   1.3 times Rust. Down from 6.996 ms, after two changes: the checksum path
   (6.279 ms per pass to about 1 ms) and placing the body directly into the
   output instead of copying it there.
   Open task: the last 1 ms is the remaining copy into the wire buffer, which
   needs a scatter write. That changes the public surface and needs a decision.
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
ipc decode and on both zstd metrics, second on ipc encode and json decode, and
third on json encode behind Zig and Rust. `bench/compare.py --check` exits 1 on
the single remaining loss, ipc encode, and that is the current state of the
work.

