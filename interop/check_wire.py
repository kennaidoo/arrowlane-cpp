#!/usr/bin/env python3
"""Cross-language wire compatibility check.

Writes a frame with the Python package, reads it with the C++ binary, then
writes a frame with the C++ binary and reads it with Python. A mismatch fails
the run. Values are compared with the canonical column digest.

Usage:
    python interop/check_wire.py --exe build/arrowlane-cpp.exe --lanes json
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import subprocess
import sys
import tempfile
from decimal import Decimal
from pathlib import Path

import pyarrow as pa

sys.path.insert(0, str(Path(__file__).resolve().parent))
from canonical import column_crc32  # noqa: E402

sys.path.insert(0, os.environ.get("ARROWLANE_SRC", "C:/Users/Ken/projects/personal-projects/arrowlane/src"))
import arrowlane  # noqa: E402


def sample_table(rows: int = 500) -> pa.Table:
    """A table with the types the JSON lane has to tag. It must stay in step
    with `make_fixture` in the C++ library."""
    schema = pa.schema(
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
    columns = [
        pa.array(range(rows), type=pa.int64()),
        pa.array([f"row-{index}" if index % 7 else None for index in range(rows)]),
        pa.array([index / 3.0 for index in range(rows)]),
        pa.array([index % 2 == 0 for index in range(rows)]),
        pa.array(
            [datetime.date(2026, 1, index % 28 + 1) for index in range(rows)],
            type=pa.date32(),
        ),
        pa.array([Decimal("12.34") for _ in range(rows)], type=pa.decimal128(9, 2)),
        pa.array(
            [
                datetime.datetime(2026, 10, 28, 12, 0, tzinfo=datetime.UTC)
                for _ in range(rows)
            ],
            type=pa.timestamp("us", tz="UTC"),
        ),
        pa.array([b"\x00\x01\x02" for _ in range(rows)], type=pa.binary()),
    ]
    return pa.Table.from_arrays(columns, schema=schema)


def run(exe: Path, args: list[str]) -> bytes:
    result = subprocess.run([str(exe), *args], capture_output=True, check=False)
    if result.returncode != 0:
        raise SystemExit(
            f"arrowlane-cpp {args} failed rc={result.returncode}\n"
            f"stdout={result.stdout[:2000]!r}\nstderr={result.stderr[:2000]!r}"
        )
    return result.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--rows", type=int, default=500)
    parser.add_argument("--lanes", default="json,ipc,ipc-zstd")
    args = parser.parse_args()
    if not args.exe.exists():
        raise SystemExit(f"binary not found: {args.exe}")

    table = sample_table(args.rows)
    failures: list[str] = []
    lanes = [lane for lane in args.lanes.split(",") if lane]

    with tempfile.TemporaryDirectory() as raw_tmp:
        tmp = Path(raw_tmp)

        for lane in lanes:
            # Direction 1: Python writes, C++ reads.
            wire = arrowlane.pack(table, lane=lane)
            path = tmp / f"py-{lane}.aln"
            path.write_bytes(wire)
            out = run(args.exe, ["unpack", str(path), "--json"])
            payload = json.loads(out)
            if payload["rows"] != table.num_rows:
                failures.append(
                    f"py->cpp {lane}: rows {payload['rows']} != {table.num_rows}"
                )
            if payload["lane"] != lane:
                failures.append(f"py->cpp {lane}: lane read as {payload['lane']}")
            if payload["columns"] != len(table.column_names):
                failures.append(f"py->cpp {lane}: column count differs")
            for index, name in enumerate(table.column_names):
                want = column_crc32(table[name])
                got = payload["schema"][index]["crc32"]
                if got != want:
                    failures.append(
                        f"py->cpp {lane}: column {name} digest {got} != {want}"
                    )

            # Direction 2: C++ writes, Python reads.
            path = tmp / f"cpp-{lane}.aln"
            run(
                args.exe,
                ["pack", "--lane", lane, "--rows", str(args.rows), "--out", str(path)],
            )
            if not path.exists():
                failures.append(f"cpp->py {lane}: no file written")
                continue
            try:
                out_table = arrowlane.unpack(path.read_bytes())
            except Exception as exc:  # noqa: BLE001 - report any decode failure
                failures.append(f"cpp->py {lane}: python could not read it: {exc}")
                continue
            if out_table.num_rows != table.num_rows:
                failures.append(
                    f"cpp->py {lane}: rows {out_table.num_rows} != {table.num_rows}"
                )
            if out_table.schema != table.schema:
                failures.append(
                    f"cpp->py {lane}: schema differs\n{out_table.schema}\n{table.schema}"
                )
            else:
                for column in table.column_names:
                    if not out_table[column].equals(table[column]):
                        failures.append(f"cpp->py {lane}: column {column} differs")
                        break

    if failures:
        print("FAIL")
        for line in failures:
            print(f"  {line}")
        return 1
    print(
        f"OK: {args.rows} rows, {len(lanes)} lane"
        f"{'' if len(lanes) == 1 else 's'}, both directions, "
        "values and schema identical"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
