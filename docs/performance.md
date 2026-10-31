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
hits all four together. Each row is the median of the rounds with the range in brackets. The verdict is
taken on the ratio per round, not on two separate ranges: both sides are measured
inside the same round, so dividing them cancels the machine's mood. A win needs
every round under the threshold, which is a stronger claim than a median under
it. Machine: desktop, Ryzen 7 5800X3D, Windows 11, Docker Desktop
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

Run of 2026-10-31 09:55, recorded in `bench/results/latest.md`. Only a full
record run writes that file; a smaller run writes `scratch-<stamp>.md` instead.

### ipc lane

| Impl | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| cpp | 12670881 | 1.847 [1.550..2.187] | 1.786 [1.400..2.163] |
| python | 12670845 | 22.859 [22.052..22.893] | 3.580 [3.399..4.958] |
| rust | 12671117 | 1.926 [1.755..2.252] | 2.844 [2.807..2.868] |
| zig | 12670849 | 45.428 [45.219..45.429] | 27.501 [26.166..28.196] |

### json lane

| Impl | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| cpp | 42505784 | 246.668 [243.798..254.676] | 589.337 [586.845..592.738] |
| python | 42505756 | 2290.481 [2249.773..2300.864] | 2157.549 [2103.980..2171.663] |
| rust | 42505812 | 267.012 [252.902..272.253] | 1171.603 [1164.418..1234.781] |
| zig | 42372426 | 190.893 [190.671..193.992] | 351.893 [335.849..356.158] |

### ipc-zstd lane

| Impl | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| cpp | 1608747 | 29.806 [28.912..30.943] | 10.257 [9.393..11.923] |
| python | 1620041 | 32.119 [29.884..32.818] | 22.097 [21.200..24.189] |
| rust | 1625309 | 28.465 [27.107..30.457] | 11.165 [10.728..12.033] |
| zig | 1620045 | 64.609 [63.422..66.830] | 22.682 [22.590..24.746] |

On the zstd lane this implementation has the smallest wire frame and the fastest
decode, 10.257 against Rust's 11.165. Encode is not the fastest: Rust takes 4.5
percent less time, and Python takes 7.8 percent more. It is not a scored row, so
none of it is claimed as a win.

## Win conditions

Ratios are C++ divided by the reference, per round. A win needs the high end of
the ratio range under the threshold.

| Condition | Ratio | Threshold | C++ | Reference | Verdict |
|---|---|---|---|---|---|
| ipc encode | 1.052 [0.688..1.136] | 0.90 | 1.847 | rust 1.926 | UNDETERMINED |
| ipc decode | 0.636 [0.488..0.761] | 0.80 | 1.786 | rust 2.844 | WIN |
| json encode | 0.110 [0.106..0.111] | 1.10 | 246.668 | python 2290.481 | WIN |
| json decode | 0.504 [0.477..0.506] | 0.60 | 589.337 | rust 1171.603 | WIN |
| ipc wire bytes | 1.000 [1.000..1.000] | 1.02 | 12670881 | python 12670845 | WIN |

Four wins, one undetermined, no losses.

## Undetermined: ipc encode

This row is not settled and is not claimed. Three runs measured it three ways:

| Run | Repeats | Rounds | Median ratio | Range | Verdict |
|---|---|---|---|---|---|
| four way, 09:55 | 7 | 3 | 1.052 | 0.688..1.136 | undetermined |
| focused rerun | 25 | 3 | 0.835 | 0.687..1.090 | undetermined |
| pinned to two cores | 15 | 9 | 0.835 | 0.403..1.064 | undetermined |

Across fifteen rounds the ratio spans 0.403 to 1.136. In one round C++ takes 60
percent less time than Rust, in another it takes 14 percent more. The threshold
is 0.90, and the data straddles it.

Pinning the containers to two cores halved the spread, from 0.688..1.136 to
0.722..0.944 in the first pinned run. In the nine round pinned run, 7 of 9
rounds are under the threshold, and a 95 percent bootstrap interval for the
median ratio is 0.812 to 1.038, so the threshold is not excluded.

An earlier run reported this row as a win at 1.306 against 1.883. That reading
came from comparing two unpaired ranges, which threw away the fact that both
sides were measured in the same round. The corrected method does not support the
claim, so the claim is withdrawn.

What would settle it: a quiet machine with the benchmark pinned to dedicated
cores, or enough rounds that the ratio's spread narrows under 0.90. Neither is
available on this desktop, where the WSL2 backend shares the CPU with Windows.

The likely cause is that 1.5 ms is a short measurement: the ipc lane takes about
2 ms end to end, so one scheduler interruption during a repeat moves the median.
The json lane, at 250 to 2000 ms, averages the same interruptions out.

## Losses

Every missed target, with the number and the open task behind it.

1. **Release binary, 1,122,816 bytes against a 512 KB target.** 2.2 times the
   target, and the target is withdrawn rather than met: the smallest full
   feature build is 1,025,536 bytes, and the smallest build at all, with zstd
   compiled out, is 644,096 bytes. The measurements are in Binary size above.
2. **Startup time, not measured.** The Rust and Zig images are scratch images
   with no shell, so `--version` cannot be timed inside them. This row is
   unproven, not won.
3. **Peak RSS at 500000 rows, not measured for all four.** Same reason. The C++
   number is taken from the operating system in `bench --json`.

Two targets closed on 2026-10-31. The json allocation target went from 163,731
to 209 allocations for 1000 rows against a target of 5,000, with the 606 byte
golden payload unchanged. The ipc encode row moved from 6.996 ms to between 1.3
and 1.9 ms after the checksum, the body placement and the in place wire write,
which turned it from a code question into a measurement question.

## Allocation budgets

`test/alloc_test.cpp` counts every allocation with a global operator new and
pins the measured values.

| Case | Allocations | Bytes |
|---|---|---|
| json encode, 1000 rows | 209 | 735153 |
| ipc encode, 1000 rows | 366 | 80730 |
| ipc view decode, 63001 byte payload | 285 | 14856 |
| ipc owning decode, 63001 byte payload | 291 | 139430 |

Three things to read from that table. The view decode allocates 14,856 bytes for
a 63,001 byte payload, so it copies no payload, which is the zero copy claim
proven rather than asserted. ipc encode allocates one payload plus 18 KB, down
from 141,993 bytes, because the body is written straight into the wire. And json
encode allocates 209 times for 1000 rows, down from 163,731, because every value
is appended into the payload instead of being built as an object first.

## Binary size

| Build | zstd | Bytes |
|---|---|---|
| release, MSVC | linked | 1,122,816 |
| size optimised, MSVC | linked | 1,025,536 |
| release, MSVC | compiled out | 717,824 |
| size optimised, MSVC | compiled out | 644,096 |

Reproduce with `scripts\measure-size.cmd`. `arrowlane_capi.dll` is 1,000,448
bytes in a release build.

The 512 KB target is not reachable and is withdrawn, see decision D4. zstd
accounts for 381,440 bytes of the difference, and even with the zstd lane
compiled out the binary is 644,096 bytes, 1.26 times the target. The shipped
number for the full feature set is 1,025,536 bytes in a size optimised build,
or 1,122,816 in the release build that `scripts\build.cmd Release` produces.

Single files, no runtime dependency beyond the C++ runtime.

## What is not claimed

Nothing here says this is the fastest implementation overall. It is fastest on
ipc decode and zstd decode, ahead of Rust on both ipc medians, and behind Zig on
both json rows: 1.29 times on encode, 1.67 times on decode. The ipc encode row is
undetermined and is not counted.

The json lane is the weak point against Zig and the strongest row against Python,
where the ratio is 0.110, that is 9 times faster. `bench/compare.py --check`
exits 1 while the ipc encode row is undetermined, and that is the correct state
of the work.

