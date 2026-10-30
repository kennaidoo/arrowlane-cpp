# Third party notices

## crc32fast 1.5.3

The PCLMULQDQ checksum in `src/crc32_pclmul.cpp` is a port of
`src/specialized/pclmulqdq.rs` from the crc32fast crate, version 1.5.3, which is
licensed MIT OR Apache-2.0. The algorithm follows the Intel white paper "Fast
CRC computation for generic polynomials using PCLMULQDQ instruction" and zlib's
implementation of it. The constants are unchanged from the crate.

## zstd 1.5.7

Fetched by `tools/fetch-zstd.sh` or `tools/fetch-zstd.cmd`, pinned by SHA256,
and linked statically into the `ipc-zstd` lane. Licensed BSD 3-Clause. Not
committed to this repository: `vendor/` is ignored and rebuilt on demand.
