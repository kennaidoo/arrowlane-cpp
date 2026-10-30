#!/usr/bin/env python3
"""Write a small Arrow IPC stream fixture for the C++ reader test.

The table matches `make_fixture` in the C++ library, so the test can compare
column digests instead of a hand written expectation.

Usage:
    python interop/make_fixture.py --rows 3 --batches 1 --out test/fixtures/pyarrow_3row.aln
"""

from __future__ import annotations

import argparse
import datetime
from decimal import Decimal
from pathlib import Path

import pyarrow as pa


def sample_table(rows: int) -> pa.Table:
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rows", type=int, default=3)
    parser.add_argument("--batches", type=int, default=1)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    table = sample_table(args.rows)
    sink = pa.BufferOutputStream()
    with pa.ipc.new_stream(sink, table.schema) as writer:
        if args.batches <= 1:
            writer.write_table(table)
        else:
            step = max(1, args.rows // args.batches)
            start = 0
            while start < args.rows:
                writer.write_table(table.slice(start, min(step, args.rows - start)))
                start += step
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(sink.getvalue().to_pybytes())
    print(f"wrote {args.out} ({args.out.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
