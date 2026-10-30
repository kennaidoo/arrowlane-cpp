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

Reverse if the comparison work needs public review before the release.

## D2 Version numbering: 0.1.0 now, 1.0.0 at the release

Stay on 0.1.0 through Phases 5 to 8. Tag 1.0.0 at the Phase 9 release.

Reason: a 0.x number says the wire format and the public API may still move.
Frame format v1 is already frozen, so 1.0.0 is the promise that it stays
frozen. Do not adopt the Python package's 0.3.1 line: this is a new codebase.
The documentation states the relationship instead, "reads and writes arrowlane
frame v1 and interoperates with arrowlane 0.3.1".

Reverse if the wire format has to change before Phase 9, in which case the
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
