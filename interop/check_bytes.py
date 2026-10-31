"""Encode one table with both implementations and compare what they produce.

The golden test pins three rows. This one feeds the Python table to the C++ CLI as
an IPC stream and compares the output for the same input, so a difference is a
codec difference and not a fixture difference.

What the two implementations are measured to share, 2026-10-31:

- the json payload, byte for byte, at 200,000 rows;
- the decoded ipc schema, and every data buffer of every column, byte for byte.

What they do not share is the flatbuffer layout of the ipc metadata: the schema
message and the record batch metadata differ in length, because pyarrow and this
port build the same flatbuffer with different padding and field order. So the ipc
check compares decoded buffers rather than raw stream bytes, and says so.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def first_difference(left: bytes, right: bytes) -> str:
    limit = min(len(left), len(right))
    for index in range(limit):
        if left[index] != right[index]:
            start = max(0, index - 24)
            return (
                f"first difference at byte {index} of {limit}\n"
                f"  python: {left[start:index + 24]!r}\n"
                f"  cpp   : {right[start:index + 24]!r}"
            )
    return f"lengths differ: python {len(left)}, cpp {len(right)}"


def pack(exe: str, lane: str, source: Path, target: Path) -> bytes:
    """Run the CLI and return the payload of the frame it wrote."""
    from arrowlane.frame import read_frame

    subprocess.run(
        [exe, "pack", "--lane", lane, "--in", str(source), "--out", str(target)],
        check=True,
    )
    with target.open("rb") as stream:
        return read_frame(stream).payload


def compare_json(exe: str, lane_type, table, work: Path) -> list[str]:
    """The json payload must be identical byte for byte."""
    cpp = pack(exe, "json", work / "fixture.arrows", work / "json.aln")
    expected = lane_type().encode(table).payload
    if cpp == expected:
        print(f"OK: json payload identical, {len(expected)} bytes")
        return []
    print("FAIL: json payload differs")
    print(first_difference(expected, cpp))
    return ["json"]


def compare_ipc(exe: str, lane_type, table, work: Path) -> list[str]:
    """The ipc data must match; the flatbuffer metadata layout is free to differ."""
    import pyarrow as pa

    cpp = pack(exe, "ipc", work / "fixture.arrows", work / "ipc.aln")
    expected = lane_type().encode(table).payload

    py_reader = pa.ipc.open_stream(expected)
    cpp_reader = pa.ipc.open_stream(cpp)
    failures: list[str] = []
    if py_reader.schema != cpp_reader.schema:
        print("FAIL: ipc schemas differ")
        failures.append("ipc schema")

    checked = 0
    for py_batch, cpp_batch in zip(py_reader, cpp_reader, strict=True):
        for column_index, (py_column, cpp_column) in enumerate(
            zip(py_batch.columns, cpp_batch.columns, strict=True)
        ):
            for slot, (py_buffer, cpp_buffer) in enumerate(
                zip(py_column.buffers(), cpp_column.buffers(), strict=True)
            ):
                checked += 1
                py_bytes = b"" if py_buffer is None else py_buffer.to_pybytes()
                cpp_bytes = b"" if cpp_buffer is None else cpp_buffer.to_pybytes()
                if py_bytes != cpp_bytes:
                    print(f"FAIL: ipc column {column_index} buffer {slot} differs")
                    print(first_difference(py_bytes, cpp_bytes))
                    failures.append(f"ipc col{column_index}.buf{slot}")

    if not failures:
        print(
            f"OK: ipc data identical, {checked} buffers across "
            f"{table.num_rows} rows (metadata layout differs by design)"
        )
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--rows", type=int, default=200_000)
    args = parser.parse_args()

    sys.path.insert(0, os.environ.get("ARROWLANE_SRC", "/arrowlane/src"))
    sys.path.insert(0, os.environ.get("ARROWLANE_BENCH", "/arrowlane/bench/containers"))

    from fixture import sample_table

    from arrowlane.lanes.ipc_lane import IpcLane
    from arrowlane.lanes.json_lane import JsonLane

    table = sample_table(args.rows)
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as work:
        directory = Path(work)
        (directory / "fixture.arrows").write_bytes(IpcLane().encode(table).payload)
        failures += compare_json(args.exe, JsonLane, table, directory)
        failures += compare_ipc(args.exe, IpcLane, table, directory)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
