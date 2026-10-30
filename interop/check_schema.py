#!/usr/bin/env python3
"""Check the C++ schema message writer and reader against pyarrow.

The writer must produce a message that `pyarrow.ipc.read_schema` accepts and
compares equal. The reader must reject a nested type with a typed error.

Usage:
    python interop/check_schema.py --exe build/arrowlane-cpp.exe
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

import pyarrow as pa


def run(exe: Path, args: list[str]) -> bytes:
    result = subprocess.run([str(exe), *args], capture_output=True, check=False)
    if result.returncode != 0:
        raise SystemExit(
            f"arrowlane-cpp {args} failed rc={result.returncode}\n"
            f"stderr={result.stderr[:2000]!r}"
        )
    return result.stdout


def expected(case: str) -> pa.Schema:
    if case == "one":
        return pa.schema([pa.field("a", pa.int64(), nullable=False)])
    if case == "fixture":
        return pa.schema(
            [
                ("id", pa.int64()),
                ("name", pa.string()),
                ("score", pa.float64()),
                ("ok", pa.bool_()),
                ("day", pa.date32()),
                ("amount", pa.decimal128(9, 2)),
                ("ts", pa.timestamp("us", tz="UTC")),
                ("blob", pa.binary()),
            ]
        )
    if case == "types":
        return pa.schema(
            [
                pa.field("n", pa.null(), nullable=True),
                pa.field("lb", pa.large_binary(), nullable=False),
                pa.field("lu", pa.large_utf8(), nullable=False),
                pa.field("u32", pa.uint32(), nullable=False),
                pa.field("f16", pa.float16(), nullable=False),
                pa.field("d64", pa.date64(), nullable=False),
                pa.field("t32", pa.time32("ms"), nullable=False),
                pa.field("t64", pa.time64("ns"), nullable=False),
                pa.field("ts", pa.timestamp("s"), nullable=False),
                pa.field("dur", pa.duration("ms"), nullable=False),
            ]
        )
    raise SystemExit(f"unknown case {case}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args()
    if not args.exe.exists():
        raise SystemExit(f"binary not found: {args.exe}")

    failures: list[str] = []
    for case in ("one", "fixture", "types"):
        data = run(args.exe, ["debug-schema", case])
        got = pa.ipc.read_schema(pa.py_buffer(data))
        want = expected(case)
        if not got.equals(want, check_metadata=True):
            failures.append(f"{case}: pyarrow reads\n{got}\nwant\n{want}")

    fixture = run(args.exe, ["debug-schema", "fixture"])
    with tempfile.NamedTemporaryFile(suffix=".aln", delete=False) as handle:
        handle.write(fixture)
        self_path = handle.name
    self_read = subprocess.run(
        [str(args.exe), "debug-schema", "read", self_path], capture_output=True
    )
    if self_read.returncode != 0 or b"ok 8 columns" not in self_read.stdout:
        failures.append(f"self read: rc={self_read.returncode} stdout={self_read.stdout!r}")

    list_schema = pa.schema([pa.field("l", pa.list_(pa.int64()), nullable=True)])
    with tempfile.NamedTemporaryFile(suffix=".aln", delete=False) as handle:
        handle.write(list_schema.serialize().to_pybytes())
        list_path = handle.name
    rejected = subprocess.run(
        [str(args.exe), "debug-schema", "read", list_path], capture_output=True
    )
    if rejected.returncode == 0 or b"UnsupportedType" not in rejected.stderr:
        failures.append(
            f"list schema: rc={rejected.returncode} stderr={rejected.stderr[:200]!r}"
        )

    if failures:
        print("FAIL")
        for line in failures:
            print(f"  {line}")
        return 1
    print("OK: 3 schema cases match pyarrow")
    return 0


if __name__ == "__main__":
    sys.exit(main())
