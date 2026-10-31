# Decisions

One decision per entry. Each states the choice, the reason, and what would
reverse it.

## D1 Repository visibility: private until the release gate

Keep the repository private while the work is in progress. Publish at the
Phase 9 release gate, and only when that gate is green: MSVC gate, Wolfi
container gate, three lane wire check, selector parity, no secrets, docs free
of personal paths.

Reason: GitHub Free allows 2,000 private Actions minutes a month, which covers
a solo three platform matrix, so free public runners are not a reason to
publish early. The reason to publish is credibility for the benchmark, and that
only exists once there is a measured release.

Outcome 2026-10-31: every gate item is green. The MSVC gate passes 24 of 24
binaries, the Wolfi container passes 24 tests plus six interop checks, the three
lane wire check and the selector parity check pass, history holds no secrets,
and no tracked file contains a personal path. One benchmark row, ipc encode
against Rust, is undetermined on this machine; `docs/performance.md` records it
with three runs and an interval. That is a limit of measurement precision, not a
code defect, so it does not block publication.

Published 2026-10-31 at https://github.com/kennaidoo/arrowlane-cpp. The first
public run of the ci workflow passed both jobs: linux in 59 seconds, windows in
1 minute 31 seconds.

Reverse if the comparison work needs public review before the release.

## D2 Version numbering: 0.1.0 until a published release

Decision on 2026-10-31: stay on 0.1.0. The earlier plan was to tag 1.0.0 at the
Phase 9 gate, and that was done, then reversed: nothing has been published, and a
1.0.0 tag on an unreleased repository is a claim with nothing behind it.

Reason: a 0.x number says the wire format and the public API may still move. The
wire format itself is frozen at format version 1 and is checked against pyarrow
on every run, which is a separate promise from the package version. Do not adopt
the Python package's 0.3.1 line: this is a new codebase. The documentation states
the relationship instead, "reads and writes arrowlane frame v1 and interoperates
with arrowlane 0.3.1".

Reverse when the repository is published and the first release is cut. Then the
package version becomes 1.0.0 and the tag follows the published artifact, not the
gate.

Historical note: the wire format has to change before Phase 9, in which case the
release stays 0.x.

## D3 PCLMULQDQ CRC32: out of scope unless measured above the trigger

Do not implement the hardware CRC32 path for 1.0.0. Trigger to include it: the
Phase 10 benchmark measures CRC above 5 percent of total frame encode time on a
100 MB frame, release build. Below the trigger, record the measurement in
`docs/performance.md` and close the task as measured and not needed.

Reason: the plan's rule is that an optimisation lands only after a measurement.
The CRC runs once per frame, so the gain is bounded by that share, and the
portable cost is real: an intrinsic path, a CPUID dispatch, and a fallback that
must also pass on the container.

Reverse if the measurement clears the trigger, or if a lane profile shows CRC
as the top entry.

## Open

Joining the Python repository's Docker benchmark: do it after the 1.0.0 tag,
because that harness expects a stable CLI and a release binary.

## D4 Binary size: the 512 KB target is withdrawn

The target was written before the zstd lane existed. Measured on 2026-10-31:

| Build | zstd | Bytes |
|---|---|---|
| release | linked | 1,122,816 |
| size optimised | linked | 1,025,536 |
| release | compiled out | 717,824 |
| size optimised | compiled out | 644,096 |

The smallest full feature build is 1,025,536 bytes, and even without zstd the
binary is 644,096, so no configuration reaches 512 KB. Keeping a target that no
build can meet would make the record dishonest.

Reason: static zstd costs about 400 KB, and the CLI carries describe, select,
pack, unpack and bench. A smaller artifact would mean a separate library only
build or a shared zstd, which is a packaging decision rather than an
optimisation.

Reverse if a shared zstd or a library only build is wanted for a specific
deployment, in which case the target returns with a stated configuration.

## D5 ipc encode: recorded undetermined, not a loss

The paired ratio for ipc encode has been measured three times, over fifteen
rounds, and every reading straddles the 0.90 threshold: 1.052 [0.688..1.136]
over three rounds, 0.835 [0.687..1.090] over three focused rounds, and 0.835
[0.403..1.064] over nine rounds pinned to two cores. Seven of the nine pinned
rounds are under the threshold, and the 95 percent bootstrap interval for the
median is 0.812 to 1.038, so the threshold is not excluded.

Reason: a row that cannot be called a win must not be called a win, and it must
not be called a loss either. The lane takes about 2 ms end to end, so one
scheduler interruption during a repeat moves the median, and this desktop shares
its CPU between Windows and the WSL2 backend that runs the benchmark. No further
optimisation of this lane is authorised on the strength of this row alone.

Reverse if a quiet machine with dedicated cores, or a second architecture,
produces a ratio whose interval excludes 0.90 in either direction. The arm64
benchmark job added for that reason is the first such attempt.

Reversed 2026-10-31. The arm64 job measured it on `ubuntu-24.04-arm`: ratio 0.975
with a 95 percent interval of 0.928 to 0.992, against the 0.90 threshold. The
interval excludes 0.90 from above, so the row is a LOSS. C++ 1.653 ms against Rust
1.696 ms, 200000 rows, seven repeats over three rounds. The undetermined verdict is
withdrawn, and this port is 2.5 percent faster than the Rust reference where the
condition asks for 10. No further work on this lane is authorised on the strength
of the row, because the row is now a known loss rather than an open question.

## D6 Byte identity: the data matches, the flatbuffer layout does not

Measured 2026-10-31 by feeding one table to both implementations through the CLI
and comparing the output:

| Part | Result |
|---|---|
| json payload, 200000 rows | identical, 42504990 bytes on both sides |
| ipc schema | equal as a schema |
| ipc data buffers | identical, 18 of 18, byte for byte |
| ipc schema message | python 472 bytes, this port 488 |
| ipc record batch metadata | python 504 bytes, this port 496 |
| frame header carrying the schema | differs, which is the 28 byte wire gap |

Reason: the two build the same flatbuffer with different padding, field order and
vtable placement, and pyarrow additionally writes its own version marker. Matching
that byte for byte would mean reproducing another library's builder decisions,
which buys nothing: every reader in the comparison decodes both streams to the same
tables, and the payload the user sees in the json lane is identical.

The consequence for the benchmark: `wire_bytes` is not a like for like comparison
across implementations, because it includes metadata whose size is a builder
choice. Read the wire column as a size estimate, not as a byte identity claim.

The row therefore prints `INFO` and counts as neither a win nor a loss. Its 1.02
threshold is not a target: it is a blow up detector, so that a change which grows
the metadata past two percent fails the run instead of passing unnoticed.

Reverse if a consumer needs hash equality on whole frames, in which case the target
is pyarrow's exact layout and the check in `interop/check_bytes.py` becomes the
acceptance test.
