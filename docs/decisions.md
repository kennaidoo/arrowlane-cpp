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
