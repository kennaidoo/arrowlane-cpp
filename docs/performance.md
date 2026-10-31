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
| ipc encode | 0.975 [0.928..0.992] | 0.90 | 1.653 | rust 1.696 | LOSS (arm64) |
| ipc decode | 0.636 [0.488..0.761] | 0.80 | 1.786 | rust 2.844 | WIN |
| json encode | 0.110 [0.106..0.111] | 1.10 | 246.668 | python 2290.481 | WIN |
| json decode | 0.504 [0.477..0.506] | 0.60 | 589.337 | rust 1171.603 | WIN |
| ipc wire bytes | 1.000 [1.000..1.000] | 1.02 | 12670881 | python 12670845 | INFO (D6) |

Three wins, no undetermined rows, one loss, one informational row. The
informational row measures the flatbuffer builder's layout, not the codec, and
D6 records why. The ipc encode row was settled on
arm64: 0.975 [0.928..0.992] against a 0.90 threshold, a 2.5 percent advantage
where the condition asks for 10.

## ipc encode: undetermined on x86, a loss on arm64

Settled 2026-10-31 on `ubuntu-24.04-arm`, the same runner that publishes the
`bench-arm64` artifact. Seven repeats over three rounds, both implementations
measured in the same round:

| Reading | Value |
|---|---|
| ratio | 0.975 |
| 95 percent interval | 0.928 to 0.992 |
| threshold | 0.90 |
| C++ | 1.653 ms [1.575..1.664] |
| Rust | 1.696 ms [1.678..1.698] |

The interval excludes 0.90 from above, so the row is a LOSS and D5 reverses on its
own stated condition. This port is 2.5 percent faster than the Rust reference on
ipc encode, where the condition asks for 10 percent. The x86 history below stays as
the record of why the row was parked.

A second run of the same job, twenty minutes later, put the interval at 0.896 to
0.928 with a ratio of 0.905, which straddles the threshold and prints
UNDETERMINED. Both readings agree on the only thing the condition asks, which is
whether the high end clears 0.90, and neither does. The row stays recorded as a
loss: the condition is not met on this architecture, and the label moves with the
runner rather than with the code.

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

The decision is D5 in `docs/decisions.md`: the row stays undetermined, it is not
a loss, and no further work on this lane is authorised on the strength of it.

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

## Where the ipc encode time goes

Measured 2026-10-31 on the desk, 200000 rows. The two paths are timed inside one
process, alternating, so the machine's mood cancels and the number is a paired
ratio. Twelve pairs, and the arena served exactly one allocation per encode:

| Reading | Value |
|---|---|
| ipc encode, payload allocated fresh on every call | 3.41 to 3.87 ms |
| ipc encode, payload served from a pre-touched arena | 1.50 to 1.69 ms |
| paired ratio, arena over allocating | 0.437 to 0.450 |

So more than half of the Windows encode is first touch faults on the 12.67 MB
payload allocation: about 2.0 ms of 3.5 ms. A caller that keeps one buffer removes
that cost.

Component probes with the same toolchain, measured separately:

| Step | Time |
|---|---|
| copy 12670881 bytes into a buffer that is already resident | 0.51 ms |
| allocate a fresh 12670881 byte buffer and touch every page | 1.31 ms |

The component probe understated the effect, and this document said a third of the
cost was first touch. That was wrong: the encoder allocates more than once on its
way to the final size, and measuring one allocation in isolation misses the rest.
The paired measurement is the one to trust.

The same source built with GCC 16 in the container reaches 1.02 ms on this machine,
which is close to the arena number less the smaller allocations it still pays. The
Linux path therefore already avoids most of this cost.

`encode_ipc_into` now takes a caller buffer and was measured the same way, both
paths alternating inside one process:

| Reading | Value |
|---|---|
| `encode_ipc`, payload allocated on every call | 2.29 to 2.57 ms |
| `encode_ipc_into`, one reused buffer | 0.83 to 0.93 ms |
| paired ratio | 0.332 to 0.343 |

Three times faster on Windows, and faster than the container build of the same
source, which takes 1.02 ms. `encode_ipc` and `encode_ipc_into` share one builder:
the stream is assembled first, so its size is known before anything is written, and
a caller buffer that is too small is refused without a partial write.

The allocating path stays the default, so nothing that exists today changes.

Decomposed with a three row fixture, which isolates the metadata from the body:

| Fixture | Reuse path |
|---|---|
| 3 rows, 1224 bytes, metadata and no real body | 0.005 to 0.010 ms |
| 200000 rows, 12670072 bytes | 0.67 to 0.68 ms |
| a warm copy of 12670072 bytes | 0.50 to 0.54 ms |

The metadata is free at this size, and the encode costs 1.3 times the copy it has to
make. A contiguous payload cannot be produced for less than one copy, so there is no
second factor of two here. The remaining 0.15 ms is the per column assembly around
the copies: eight columns, their validity bitmaps and their offsets.

The loss recorded in D5 stands: 0.975 and 0.905 on arm64 are both short of 0.90,
because the comparison runs in containers where the allocation cost is already
absent.

## The Zig build mode, measured 2026-10-31

The Zig benchmark image is built with `-Doptimize=ReleaseSafe`, while this port is
Release and Rust is built with `--release`. That is a different trade-off in the
same table, so the image was rebuilt with `-Doptimize=ReleaseFast` and both were run
interleaved at 200000 rows, five repeats, twice each:

| Lane | ReleaseSafe | ReleaseFast |
|---|---|---|
| json encode | 175.1 and 176.2 ms | 145.0 and 152.0 ms |
| ipc encode | 40.6 and 43.4 ms | 39.6 and 42.9 ms |
| ipc decode | 23.9 and 25.1 ms | 24.4 and 25.9 ms |

The safety checks are worth about 15 percent on the json encode, and nothing
measurable on ipc. So the ipc gap against this port, roughly 40 ms against 1.85 ms,
is not the build mode and not the compiler. It is the writer.

For the record, this port is 21 to 24 times faster than the Zig implementation on
ipc encode and about 1.2 times slower on json encode, and no condition in
`bench/compare.py` uses Zig as a reference, so neither number is adjudicated by the
verdict table.

## Measured and reverted

An optimisation that does not move its number is recorded here, so the same work
is not repeated.

1. **Decimal digits in base 10^9.** Dividing the four limb value by 1,000,000,000
   five times instead of by 10 up to 38 times cuts the divisions from 152 to 20
   per value. json encode at 200000 rows measured 237.714 ms before and 242.683
   ms after, medians of three runs of five repeats each, so the change sat inside
   the run to run spread and was reverted. The decimal formatter is not the json
   lane's floor. The guard test for the widest value stayed, because it exposed a
   gap in the test suite: no case covered a value that fills all 38 digits.

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

## arm64

No arm64 host is attached to this desk, so this section records the state and not
a number.

Proven:

- The library builds for aarch64 with GCC 16 under
  `docker run --platform linux/arm64`, once `<cpuid.h>` is guarded out of the two
  x86 checksum files.
- `crc32_test` prints `crc32 kernels: pclmul=0 vpclmul=0` and
  `ok 5 tests, 13 checks` there, so the ARMv8 path equals the table path on 2000
  buffers of 1 to 70000 bytes.
- The `linux-arm64` job in `.github/workflows/ci.yml` builds and tests on
  `ubuntu-24.04-arm`.

Measured 2026-10-31 on `ubuntu-24.04-arm`, a GitHub-hosted arm64 runner, at
commit `83bb8fd`, 200,000 rows and 5 repeats, from the `bench-arm64` artifact of
run 41200919226:

| Lane | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| json | 42505784 | 206.18 | 455.41 |
| ipc | 12670881 | 2.0056 | 1.9699 |
| ipc-zstd | 1608747 | 30.14 | 7.58 |

Peak RSS was 305300 KB for the process.

Two things this settles:

- **The wire is architecture independent.** All three byte counts are identical to
  the x86 run, to the byte.
- **The ARMv8 checksum runs on real hardware.** ipc encode is 2.0056 ms, against
  1.185 to 1.306 ms on the Ryzen 7 5800X3D. The table fallback was estimated at 6
  to 8 ms, so this is the instruction path, not the fallback.

This is one run on shared hardware, so it is a first number and not a verdict. It
does give D5 the second architecture it asked for, though the ipc encode ratio
there has not been measured against the Rust reference on the same runner, so the
undetermined row stays undetermined.

## What is not claimed

Nothing here says this is the fastest implementation overall. It is fastest on
ipc decode and zstd decode, ahead of Rust on ipc decode, and behind Zig on
both json rows: 1.29 times on encode, 1.67 times on decode. The ipc encode row is
a loss on arm64, 0.975 against a 0.90 threshold.

The json lane is the weak point against Zig and the strongest row against Python,
where the ratio is 0.110, that is 9 times faster. `bench/compare.py --check`
exits 1 while the ipc encode row is undetermined, and that is the correct state
of the work.

