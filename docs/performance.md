# Performance

Measured, never estimated. Every number below comes from a command in this
repository, and every missed target is listed with its number.

## Method

```bash
docker build -f bench/Dockerfile.cpp -t arrowlane-bench-cpp:local .
python bench/compare.py --rows 200000 --repeats 7 --rounds 3 --check
```

One machine, one fixture, one row count, three interleaved rounds of seven
repeats, each implementation in its own container, warm up run discarded. Every
implementation is measured inside the same round, so a thermal or load change
hits all four together. Each row below is the median of the rounds with the
range in brackets. Machine: desktop, Ryzen 7 5800X3D, Windows 11, Docker Desktop
with the WSL2 backend. Date 2026-10-31. Compiler for the C++ build: GCC 16.2.0
in the Wolfi image, release mode.

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
| cpp | 12670881 | 1.306 [1.097..1.480] | 1.399 [1.277..1.582] |
| python | 12670845 | 19.272 [18.771..19.640] | 3.436 [3.296..3.855] |
| rust | 12671117 | 1.883 [1.785..1.987] | 2.741 [2.739..2.831] |
| zig | 12670849 | 43.930 [42.961..45.679] | 26.259 [26.017..27.067] |

### json lane

| Impl | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| cpp | 42505784 | 420.511 [419.206..427.057] | 590.546 [550.509..593.088] |
| python | 42505756 | 2149.249 [2057.714..2247.899] | 2052.513 [1973.270..2079.816] |
| rust | 42505812 | 262.011 [248.045..272.111] | 1204.029 [1174.650..1237.234] |
| zig | 42372426 | 180.133 [177.324..185.798] | 339.865 [329.518..348.563] |

### ipc-zstd lane

| Impl | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| cpp | 1608747 | 28.107 [25.894..31.658] | 9.475 [9.116..10.684] |
| python | 1620041 | 27.977 [26.209..30.320] | 20.001 [19.142..21.079] |
| rust | 1625309 | 30.646 [26.199..31.082] | 11.486 [10.736..12.402] |
| zig | 1620045 | 62.386 [60.291..66.337] | 20.964 [20.697..23.807] |

On the zstd lane this implementation has the smallest wire frame and the fastest
decode by 2 ms. Encode is a three way tie that the data cannot separate: C++
28.107, Python 27.977, Rust 30.646, with ranges that overlap. It is not a scored
row in the win table, which is why none of it is claimed as a win.

## Win conditions

| Condition | C++ | Reference | Limit | Verdict |
|---|---|---|---|---|
| ipc encode | 1.306 | rust 1.883 | 1.695 | WIN |
| ipc decode | 1.399 | rust 2.741 | 2.193 | WIN |
| json encode | 420.511 | python 2149.249 | 2364.174 | WIN |
| json decode | 590.546 | rust 1204.029 | 722.417 | WIN |
| ipc wire bytes | 12670881 | python 12670845 | 12924262 | WIN |

Five wins, no losses, and no row is undetermined: no C++ range overlaps the
reference range it is measured against. The narrowest margin is ipc encode,
where the C++ high of 1.480 sits below the Rust low of 1.785.

The reference numbers move between runs because the machine does: Rust's ipc
encode measured 1.387 ms in one run, 2.656 ms in another and 1.883 ms here. That
is why the harness runs three interleaved rounds and reports ranges. A single
run cannot decide a row whose margin is smaller than the spread, and the
`UNDETERMINED` verdict exists for exactly that case. It did not trigger here.

## Losses

Every missed target, with the number and the open task behind it.

1. **Release binary, 1,122,816 bytes against a 512 KB target.** 2.2 times the
   target. About 700 KB of it is statically linked zstd. A build with
   `-DAL_BUILD_ZSTD=OFF` is smaller and cannot read the zstd lane, so the
   number above is the honest one for the full feature set.
2. **Startup time, not measured.** The Rust and Zig images are scratch images
   with no shell, so `--version` cannot be timed inside them. This row is
   unproven, not won.
3. **Peak RSS at 500000 rows, not measured for all four.** Same reason. The C++
   number is taken from the operating system in `bench --json`.
4. **json allocations, 163731 for 1000 rows against a target of 5000.** Down
   from 309673, with byte identical output, but not at the target. The rest is
   inside the value formatters: a `JsonValue` and a `std::string` per value,
   with decimal formatting the largest contributor. Open task: formatters that
   write into a caller buffer.

The ipc encode loss closed on 2026-10-31. It stood at 1.540 ms against a
1.424 ms limit, and the eight stream VPCLMULQDQ kernel took it to 1.306 ms
against a 1.695 ms limit in the next run.

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
json encode, where Zig is 2.3 times quicker and Rust 1.6 times. zstd encode is a
tie the data cannot separate.

The json lane is the honest weak point: it beats Python by 5 times and loses to
Zig by 2.3 times on the same fixture. `bench/compare.py --check` exits 0 on all
five conditions, which is a statement about those five rows on this machine, not
about every workload.

