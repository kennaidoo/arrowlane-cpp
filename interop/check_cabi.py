#!/usr/bin/env python3
"""Drive the C ABI through ctypes, the way a foreign binding would.

Loads the shared library, calls every entry point, and checks the values
against the reference. The input frame comes from the Python package, so the
ABI is exercised on foreign bytes, not on bytes this library wrote.

Usage:
    ARROWLANE_SRC=<repo>/src python interop/check_cabi.py --dll build/arrowlane_capi.dll
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import sys
from pathlib import Path

src = os.environ.get("ARROWLANE_SRC")
if not src:
    sys.exit("error: set ARROWLANE_SRC to the arrowlane reference src directory")
sys.path.insert(0, src)
sys.path.insert(0, str(Path(__file__).resolve().parent))

from arrowlane.transit import pack  # noqa: E402
from check_wire import sample_table  # noqa: E402


class AlBuffer(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.POINTER(ctypes.c_uint8)),
        ("len", ctypes.c_size_t),
        ("cap", ctypes.c_size_t),
    ]


class AlDecision(ctypes.Structure):
    _fields_ = [
        ("lane_id", ctypes.c_uint8),
        ("predicted_ms", ctypes.c_double),
        ("predicted_bytes", ctypes.c_uint64),
        # Keep the raw pointer: al_free_reason frees the block the library
        # allocated. A c_char_p field would hand back a copy, and freeing a
        # copy corrupts the heap.
        ("reason", ctypes.c_void_p),
    ]


def load(path: Path):
    library = ctypes.CDLL(str(path))
    library.al_version.restype = ctypes.c_char_p
    library.al_version.argtypes = []
    library.al_select.restype = ctypes.c_int
    library.al_select.argtypes = [
        ctypes.c_uint64,
        ctypes.c_uint64,
        ctypes.c_uint8,
        ctypes.c_uint8,
        ctypes.c_bool,
        ctypes.POINTER(AlDecision),
    ]
    library.al_inspect.restype = AlBuffer
    library.al_inspect.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
    library.al_repack.restype = AlBuffer
    library.al_repack.argtypes = [
        ctypes.POINTER(ctypes.c_uint8),
        ctypes.c_size_t,
        ctypes.c_uint8,
    ]
    library.al_free.restype = None
    library.al_free.argtypes = [AlBuffer]
    library.al_free_reason.restype = None
    library.al_free_reason.argtypes = [ctypes.c_void_p]
    return library


def as_bytes(buffer: AlBuffer) -> bytes:
    if not buffer.data or buffer.len == 0:
        return b""
    return ctypes.string_at(buffer.data, buffer.len)


def as_pointer(payload: bytes):
    array = (ctypes.c_uint8 * len(payload)).from_buffer_copy(payload)
    return array


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dll", required=True, type=Path)
    args = parser.parse_args(argv)
    path = args.dll.resolve()
    if not path.exists():
        raise SystemExit(f"error: library not found: {path}")
    library = load(path)

    version = library.al_version()
    if not version:
        print("FAIL: al_version returned nothing")
        return 1

    decision = AlDecision()
    status = library.al_select(200_000, 13_380_000, 1, 0, False, ctypes.byref(decision))
    if status != 0:
        print(f"FAIL: al_select returned {status}")
        return 1
    if decision.lane_id != 2 or abs(decision.predicted_ms - 14.8256) > 1e-4:
        print(f"FAIL: al_select chose lane {decision.lane_id} at "
              f"{decision.predicted_ms:.4f} ms")
        return 1
    if decision.predicted_bytes != 13_380_454:
        print(f"FAIL: al_select predicted {decision.predicted_bytes} bytes")
        return 1
    reason = ctypes.string_at(decision.reason) if decision.reason else b""
    if reason != b"BEST":
        print(f"FAIL: al_select reason {reason!r}")
        return 1
    library.al_free_reason(decision.reason)

    wire = pack(sample_table(200), lane="json")
    array = as_pointer(wire)
    inspected = library.al_inspect(array, len(wire))
    text = as_bytes(inspected)
    if not text:
        print("FAIL: al_inspect returned nothing for a reference frame")
        return 1
    report = json.loads(text)
    if report["rows"] != 200 or report["columns"] != 8 or report["lane"] != "json":
        print(f"FAIL: al_inspect reported {report}")
        return 1
    if len(report["digests"]) != 8:
        print(f"FAIL: al_inspect reported {len(report['digests'])} digests")
        return 1
    library.al_free(inspected)

    repacked = library.al_repack(array, len(wire), 2)
    moved = as_bytes(repacked)
    if not moved:
        print("FAIL: al_repack returned nothing")
        return 1
    array2 = as_pointer(moved)
    inspected2 = library.al_inspect(array2, len(moved))
    report2 = json.loads(as_bytes(inspected2))
    if report2["lane"] != "ipc" or report2["rows"] != 200:
        print(f"FAIL: al_repack produced {report2['lane']} with {report2['rows']} rows")
        return 1
    before = [entry["crc32"] for entry in report["digests"]]
    after = [entry["crc32"] for entry in report2["digests"]]
    if before != after:
        print(f"FAIL: repack changed the values: {before} != {after}")
        return 1
    library.al_free(inspected2)
    library.al_free(repacked)

    empty = library.al_inspect(None, 0)
    if empty.data:
        print("FAIL: al_inspect accepted a null buffer")
        return 1

    print(f"OK: version {version.decode()}, select, inspect, repack, free")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
