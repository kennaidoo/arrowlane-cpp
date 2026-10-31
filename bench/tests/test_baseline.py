"""A known loss stays quiet; a row that gets worse does not."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from compare import baseline_regressions  # noqa: E402

BASELINE = {
    "ipc_encode_ms": "LOSS",
    "ipc_decode_ms": "WIN",
    "json_encode_ms": "WIN",
    "json_decode_ms": "WIN",
    "ipc_wire_bytes": "INFO",
}


def test_unchanged_verdicts_pass():
    assert baseline_regressions(BASELINE, dict(BASELINE)) == []


def test_known_loss_stays_quiet():
    assert baseline_regressions(BASELINE, {"ipc_encode_ms": "LOSS"}) == []


def test_a_win_that_slips_is_reported():
    problems = baseline_regressions(BASELINE, {"ipc_decode_ms": "UNDETERMINED"})
    assert len(problems) == 1
    assert "ipc_decode_ms" in problems[0]


def test_a_loss_that_stays_a_loss_is_not_a_regression():
    assert baseline_regressions(BASELINE, {"ipc_encode_ms": "LOSS"}) == []


def test_a_row_the_baseline_does_not_know_is_reported():
    assert baseline_regressions(BASELINE, {"new_row": "WIN"})


def main() -> int:
    cases = [value for name, value in sorted(globals().items())
             if name.startswith("test_")]
    for case in cases:
        case()
    print(f"OK: {len(cases)} baseline cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
