#!/usr/bin/env python3
"""Selector parity check against the Python reference.

Runs a grid of workloads through `python -m arrowlane.select` and through the
C++ binary, then compares the decision field by field. The grid covers every
topology and policy, the empty and non-empty corners, allowlists, unknown
lanes, and the shm readiness flag.

Usage:
    ARROWLANE_SRC=<repo>/src python interop/check_select.py --exe build/arrowlane-cpp.exe
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

src = os.environ.get("ARROWLANE_SRC")
if not src:
    sys.exit("error: set ARROWLANE_SRC to the arrowlane reference src directory")
sys.path.insert(0, src)

from arrowlane.costmodel import load_default  # noqa: E402
from arrowlane.select import Workload, select_plan  # noqa: E402

ROWS = (0, 1, 100, 500, 1000, 200_000, 1_000_000)
BYTES = (0, 64, 1_000, 100_000, 13_380_000, 100_000_000)
TOPOLOGIES = ("same_host", "same_network", "wide_area")
POLICIES = ("latency", "throughput", "cpu")


def cases():
    """Every workload in the grid, plus the allowlist and shm variants."""
    for rows in ROWS:
        for size in BYTES:
            if rows > 0 and size == 0:
                continue
            for topology in TOPOLOGIES:
                for policy in POLICIES:
                    yield {
                        "rows": rows,
                        "bytes": size,
                        "topology": topology,
                        "policy": policy,
                    }
    for rows, size in ((1000, 1000), (200_000, 13_380_000), (0, 0), (500, 64)):
        for topology in ("same_host", "wide_area"):
            for policy in ("latency", "throughput"):
                base = {
                    "rows": rows,
                    "bytes": size,
                    "topology": topology,
                    "policy": policy,
                }
                yield {**base, "allow": "ipc"}
                yield {**base, "allow": "json,ipc"}
                yield {**base, "allow": "nope,ipc"}
                yield {**base, "shm_ready": True}


def python_decision(case: dict, model) -> dict:
    allowed = None
    if case.get("allow"):
        allowed = tuple(part.strip() for part in case["allow"].split(",") if part.strip())
    decision = select_plan(
        Workload(rows=case["rows"], arrow_bytes=case["bytes"]),
        topology=case["topology"],
        policy=case["policy"],
        model=model,
        allowed=allowed,
        shm_ready=case.get("shm_ready", False),
    )
    return json.loads(json.dumps(decision.as_dict()))


def cpp_decision(exe: Path, case: dict) -> dict:
    argv = [
        str(exe),
        "select",
        "--rows",
        str(case["rows"]),
        "--bytes",
        str(case["bytes"]),
        "--topology",
        case["topology"],
        "--policy",
        case["policy"],
        "--json",
    ]
    if case.get("allow"):
        argv += ["--allow", case["allow"]]
    if case.get("shm_ready"):
        argv += ["--shm-ready"]
    result = subprocess.run(argv, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(
            f"error: {argv[1:]} exited {result.returncode}: {result.stderr.strip()}"
        )
    return json.loads(result.stdout)


def same_float(left: float, right: float) -> bool:
    return abs(left - right) <= 1e-9


def compare(case: dict, expected: dict, actual: dict) -> str | None:
    """Return the first difference, or None."""
    for key in ("lane", "reason", "predicted_bytes"):
        if expected[key] != actual[key]:
            return f"{key}: python {expected[key]!r} != cpp {actual[key]!r}"
    if not same_float(expected["predicted_ms"], actual["predicted_ms"]):
        return f"predicted_ms: python {expected['predicted_ms']} != cpp {actual['predicted_ms']}"
    if len(expected["scores"]) != len(actual["scores"]):
        return f"scores length: python {len(expected['scores'])} != cpp {len(actual['scores'])}"
    for left, right in zip(expected["scores"], actual["scores"]):
        if left[0] != right[0]:
            return f"score name: python {left[0]!r} != cpp {right[0]!r}"
        if not same_float(left[1], right[1]):
            return f"score {left[0]}: python {left[1]} != cpp {right[1]}"
    if expected["rejected"] != actual["rejected"]:
        return f"rejected: python {expected['rejected']} != cpp {actual['rejected']}"
    return None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args(argv)
    exe = args.exe.resolve()
    if not exe.exists():
        raise SystemExit(f"error: binary not found: {exe}")
    model = load_default()

    ran = 0
    for case in cases():
        expected = python_decision(case, model)
        actual = cpp_decision(exe, case)
        difference = compare(case, expected, actual)
        if difference is not None:
            print(f"FAIL: {case}")
            print(f"  {difference}")
            return 1
        ran += 1

    # The text form must match the reference format too, once.
    case = {"rows": 200_000, "bytes": 13_380_000, "topology": "same_network",
            "policy": "latency"}
    decision = python_decision(case, model)
    reference = (
        f"lane={decision['lane']} reason={decision['reason']} "
        f"predicted_ms={decision['predicted_ms']:.3f} "
        f"predicted_bytes={decision['predicted_bytes']}"
    )
    result = subprocess.run(
        [str(exe), "select", "--rows", str(case["rows"]), "--bytes", str(case["bytes"]),
         "--topology", case["topology"], "--policy", case["policy"]],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.stdout.strip() != reference:
        print("FAIL: text form")
        print(f"  python {reference!r}")
        print(f"  cpp    {result.stdout.strip()!r}")
        return 1

    print(f"OK: {ran} cases, lane, reason, ms, bytes, scores and rejected identical")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
