# Changelog

One entry per release. The wire format version is separate from the package
version and has not changed.

## 1.0.0

First correct release. Frame format version 1.

- Frame encode and decode with CRC32, stream readers and writers, and bounded
  limits on every length that arrives from the wire.
- Arrow schema flatbuffer codec, read and write, verified against pyarrow for
  three schema cases.
- Four lanes: `json`, `ipc`, `ipc-zstd`, `shm`.
- Zero copy read path for the ipc lane, with a pointer identity test.
- Lane selector driven by the shipped cost model, with 388 parity cases against
  the Python reference.
- Chunk streams: lazy producer, validating assembler, CLI paths, and a
  cross-language check in both directions.
- C ABI with the same shape as the Zig header, exercised through ctypes.
- Fuzz harness: 15,000 byte mutations and 500 differential tables per run.
- The suite runs on MSVC and on GCC 16 in a Wolfi container, with ASan and
  UBSan in the container.

Not in this release: benchmarks against the Rust and Zig implementations, a
public repository, and any claim to be the fastest. Those are Phase 10 work.
