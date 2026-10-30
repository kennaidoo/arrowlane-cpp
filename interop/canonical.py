"""Canonical value bytes and CRC32 for a pyarrow column.

The byte layout matches `column_digest` in the C++ library and the Rust and
Zig implementations: one validity byte per row, then the value bytes. The
CRC32 of that stream is the cross language value oracle.
"""

from __future__ import annotations

import struct
import zlib

import pyarrow as pa


def _int_bytes(value: int, signed: bool) -> bytes:
    return struct.pack("<q" if signed else "<Q", value)


def canonical_bytes(column) -> bytes:
    if isinstance(column, pa.ChunkedArray):
        array = column.combine_chunks()
    else:
        array = column
    stream = bytearray()
    raw_temporal = None
    if pa.types.is_temporal(array.type) or pa.types.is_date(array.type):
        target = (
            pa.int32()
            if pa.types.is_date32(array.type) or pa.types.is_time32(array.type)
            else pa.int64()
        )
        raw_temporal = array.cast(target).to_pylist()
    values = array.to_pylist()
    kind = array.type
    scale = getattr(kind, "scale", 0)
    decimal_width = 16 if str(kind).startswith("decimal128") else 32
    for index, value in enumerate(values):
        if value is None:
            stream.append(0)
            continue
        stream.append(1)
        if pa.types.is_null(kind):
            continue
        if pa.types.is_boolean(kind):
            stream.append(1 if value else 0)
        elif pa.types.is_integer(kind):
            stream += _int_bytes(int(value), pa.types.is_signed_integer(kind))
        elif pa.types.is_float16(kind):
            stream += struct.pack("<e", float(value))
        elif pa.types.is_floating(kind):
            stream += struct.pack("<d", float(value))
        elif pa.types.is_string(kind) or pa.types.is_large_string(kind):
            raw = value.encode("utf-8")
            prefix = "<I" if pa.types.is_string(kind) else "<Q"
            stream += struct.pack(prefix, len(raw)) + raw
        elif pa.types.is_binary(kind) or pa.types.is_large_binary(kind):
            prefix = "<I" if pa.types.is_binary(kind) else "<Q"
            stream += struct.pack(prefix, len(value)) + value
        elif pa.types.is_fixed_size_binary(kind):
            stream += struct.pack("<I", len(value)) + value
        elif pa.types.is_decimal(kind):
            unscaled = int(value.scaleb(scale))
            stream += unscaled.to_bytes(decimal_width, "little", signed=True)
        elif pa.types.is_date32(kind) or pa.types.is_time32(kind):
            stream += struct.pack("<i", raw_temporal[index])
        elif raw_temporal is not None:
            stream += struct.pack("<q", raw_temporal[index])
        else:
            raise TypeError(f"canonical_bytes does not cover {kind}")
    return bytes(stream)


def column_crc32(column) -> int:
    return zlib.crc32(canonical_bytes(column))
