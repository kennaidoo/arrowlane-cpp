#!/usr/bin/env python3
"""Cross-language chunk stream check.

Writes a chunked stream with the Python reference and reads it with the C++
binary, then writes one with the C++ binary and reads it with the reference.
The reference parses every frame, so the chunk metadata and the row counts must
match, not just the total.

Usage:
    ARROWLANE_SRC=<repo>/src python interop/check_chunks.py --exe build/arrowlane-cpp.exe
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

src = os.environ.get("ARROWLANE_SRC")
if not src:
    sys.exit("error: set ARROWLANE_SRC to the arrowlane reference src directory")
sys.path.insert(0, src)
sys.path.insert(0, str(Path(__file__).resolve().parent))

from arrowlane.chunks import pack_chunks  # noqa: E402
from arrowlane.frame import read_frame  # noqa: E402
from check_wire import sample_table  # noqa: E402

ROWS = 500
PER_CHUNK = 200
EXPECTED_CHUNKS = 3


def run(argv: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(argv, capture_output=True, text=True, check=False)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args(argv)
    exe = args.exe.resolve()
    if not exe.exists():
        raise SystemExit(f"error: binary not found: {exe}")

    table = sample_table(ROWS)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)

        # Python writes, C++ reads.
        python_path = root / "python-chunks.aln"
        frames = list(pack_chunks(table, lane="ipc", rows_per_chunk=PER_CHUNK))
        if len(frames) != EXPECTED_CHUNKS:
            print(f"FAIL: reference produced {len(frames)} chunks, expected "
                  f"{EXPECTED_CHUNKS}")
            return 1
        python_path.write_bytes(b"".join(frames))
        result = run([str(exe), "unpack", "--chunks", str(python_path)])
        if result.returncode != 0:
            print(f"FAIL: cpp could not read the reference stream: "
                  f"{result.stderr.strip()}")
            return 1
        expected = f"chunks={EXPECTED_CHUNKS} rows={ROWS}"
        if result.stdout.strip() != expected:
            print(f"FAIL: cpp read {result.stdout.strip()!r}, expected {expected!r}")
            return 1

        # C++ writes, Python reads.
        cpp_path = root / "cpp-chunks.aln"
        result = run([str(exe), "pack", "--chunks", "--rows", str(ROWS),
                      "--rows-per-chunk", str(PER_CHUNK), "--lane", "ipc",
                      "--out", str(cpp_path)])
        if result.returncode != 0:
            print(f"FAIL: cpp could not write a chunk stream: {result.stderr.strip()}")
            return 1
        indexes: list[int] = []
        lasts: list[bool] = []
        ids: set[str] = set()
        rows = 0
        with cpp_path.open("rb") as stream:
            while (frame := read_frame(stream)) is not None:
                if frame.chunk is None:
                    print("FAIL: a C++ frame carries no chunk metadata")
                    return 1
                ids.add(frame.chunk.id)
                indexes.append(frame.chunk.index)
                lasts.append(frame.chunk.last)
                rows += frame.rows
        if indexes != list(range(EXPECTED_CHUNKS)):
            print(f"FAIL: reference saw indexes {indexes}")
            return 1
        if lasts != [False] * (EXPECTED_CHUNKS - 1) + [True]:
            print(f"FAIL: reference saw last flags {lasts}")
            return 1
        if rows != ROWS:
            print(f"FAIL: reference counted {rows} rows, expected {ROWS}")
            return 1
        if len(ids) != 1:
            print(f"FAIL: reference saw {len(ids)} stream ids")
            return 1
        stream_id = ids.pop()
        if len(stream_id) != 32 or any(c not in "0123456789abcdef" for c in stream_id):
            print(f"FAIL: stream id {stream_id!r} is not 32 lowercase hex characters")
            return 1

    print(f"OK: {ROWS} rows, {EXPECTED_CHUNKS} chunks, both directions, chunk "
          f"metadata and row counts identical")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
