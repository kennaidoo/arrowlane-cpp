#!/usr/bin/env python3
"""Differential fuzz: random tables through every lane in both directions.

For each table:
  * Python encodes each lane, the C++ binary decodes it, and the per column
    digests must match the Python canonical digests.
  * The C++ binary re-encodes the table from its IPC stream, Python decodes it,
    and every column must compare equal.

Usage:
    python interop/fuzz_wire.py --exe build/arrowlane-cpp --cases 500
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import random
import subprocess
import sys
import tempfile
from decimal import Decimal
from pathlib import Path

import pyarrow as pa

sys.path.insert(0, str(Path(__file__).resolve().parent))
from canonical import column_crc32  # noqa: E402

sys.path.insert(
    0,
    os.environ.get(
        "ARROWLANE_SRC", "C:/Users/Ken/projects/personal-projects/arrowlane/src"
    ),
)
import arrowlane  # noqa: E402

TYPES = [
    pa.bool_(),
    pa.int8(),
    pa.int16(),
    pa.int32(),
    pa.int64(),
    pa.uint8(),
    pa.uint16(),
    pa.uint32(),
    pa.uint64(),
    pa.float32(),
    pa.float64(),
    pa.string(),
    pa.large_string(),
    pa.binary(),
    pa.large_binary(),
    pa.binary(3),
    pa.decimal128(9, 2),
    pa.date32(),
    pa.date64(),
    pa.time32("ms"),
    pa.time64("us"),
    pa.timestamp("us", tz="UTC"),
    pa.duration("ms"),
]


def random_value(rng: random.Random, kind):
    if rng.random() < 0.12:
        return None
    if pa.types.is_boolean(kind):
        return rng.random() < 0.5
    if pa.types.is_integer(kind):
        bits = kind.bit_width
        if pa.types.is_unsigned_integer(kind):
            return rng.randrange(0, 2 ** min(bits, 31))
        return rng.randrange(-(2 ** min(bits - 1, 30)), 2 ** min(bits - 1, 30))
    if pa.types.is_float32(kind):
        return rng.uniform(-1e5, 1e5)
    if pa.types.is_float64(kind):
        return rng.uniform(-1e9, 1e9)
    if pa.types.is_string(kind) or pa.types.is_large_string(kind):
        return "s" + str(rng.randrange(1000))
    if pa.types.is_fixed_size_binary(kind):
        return bytes(rng.randrange(256) for _ in range(kind.byte_width))
    if pa.types.is_binary(kind) or pa.types.is_large_binary(kind):
        return bytes(rng.randrange(256) for _ in range(rng.randrange(0, 8)))
    if pa.types.is_decimal(kind):
        return Decimal(rng.randrange(-10**6, 10**6)).scaleb(-2)
    if pa.types.is_date32(kind) or pa.types.is_date64(kind):
        return datetime.date(2020, 1, 1) + datetime.timedelta(days=rng.randrange(0, 3000))
    if pa.types.is_time32(kind) or pa.types.is_time64(kind):
        return datetime.time(
            rng.randrange(24), rng.randrange(60), rng.randrange(60), rng.randrange(1000000)
        )
    if pa.types.is_timestamp(kind):
        return datetime.datetime(2020, 1, 1, tzinfo=datetime.UTC) + datetime.timedelta(
            seconds=rng.randrange(0, 10**6)
        )
    if pa.types.is_duration(kind):
        return datetime.timedelta(seconds=rng.randrange(0, 10**6))
    raise SystemExit(f"no generator for {kind}")


def random_table(rng: random.Random, rows: int) -> pa.Table:
    columns = []
    fields = []
    for index in range(rng.randint(1, 6)):
        kind = rng.choice(TYPES)
        values = [random_value(rng, kind) for _ in range(rows)]
        columns.append(pa.array(values, type=kind))
        fields.append(pa.field(f"c{index}", kind, nullable=True))
    return pa.Table.from_arrays(columns, schema=pa.schema(fields))


def run(exe: Path, args: list[str]) -> bytes:
    result = subprocess.run([str(exe), *args], capture_output=True, check=False)
    if result.returncode != 0:
        raise SystemExit(
            f"arrowlane-cpp {args} failed rc={result.returncode}\n"
            f"stderr={result.stderr[:2000]!r}"
        )
    return result.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--cases", type=int, default=500)
    parser.add_argument("--seed", type=int, default=20261030)
    parser.add_argument("--lanes", default="json,ipc,ipc-zstd")
    args = parser.parse_args()
    if not args.exe.exists():
        raise SystemExit(f"binary not found: {args.exe}")

    rng = random.Random(args.seed)
    lanes = [lane for lane in args.lanes.split(",") if lane]
    failures: list[str] = []

    with tempfile.TemporaryDirectory() as raw_tmp:
        tmp = Path(raw_tmp)
        for case in range(args.cases):
            rows = rng.choice([0, 1, 3, 17, 200])
            table = random_table(rng, rows)
            source = tmp / "source.aln"
            sink = pa.BufferOutputStream()
            with pa.ipc.new_stream(sink, table.schema) as writer:
                writer.write_table(table)
            source.write_bytes(sink.getvalue().to_pybytes())

            for lane in lanes:
                try:
                    wire = arrowlane.pack(table, lane=lane)
                except Exception as exc:  # noqa: BLE001
                    failures.append(f"case {case} {lane}: python pack failed: {exc}")
                    continue
                path = tmp / "py.aln"
                path.write_bytes(wire)
                payload = json.loads(run(args.exe, ["unpack", str(path), "--json"]))
                if payload["rows"] != table.num_rows:
                    failures.append(
                        f"case {case} {lane}: rows {payload['rows']} != {table.num_rows}"
                    )
                for index, name in enumerate(table.column_names):
                    want = column_crc32(table[name])
                    got = payload["schema"][index]["crc32"]
                    if got != want:
                        failures.append(
                            f"case {case} {lane}: column {name} digest {got} != {want}"
                        )

                out = tmp / "cpp.aln"
                run(
                    args.exe,
                    ["pack", "--in", str(source), "--lane", lane, "--out", str(out)],
                )
                try:
                    back = arrowlane.unpack(out.read_bytes())
                except Exception as exc:  # noqa: BLE001
                    failures.append(f"case {case} {lane}: python read failed: {exc}")
                    continue
                if back.schema != table.schema:
                    failures.append(f"case {case} {lane}: schema differs")
                elif not back.equals(table):
                    failures.append(f"case {case} {lane}: values differ")
            if failures:
                break

    if failures:
        print("FAIL")
        for line in failures[:20]:
            print(f"  {line}")
        return 1
    print(
        f"OK: {args.cases} random tables, {len(lanes)} lane"
        f"{'' if len(lanes) == 1 else 's'}, digests equal in both directions"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
