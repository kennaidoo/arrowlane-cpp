# Four way comparison

Run 2026-10-31T08:41:06+02:00 on Windows-11-10.0.26300-SP0 with AMD64 Family 25 Model 33 Stepping 2, AuthenticAMD.
Rows 200000, repeats 7, one lane at a time, each implementation in its own container.

## Method

| Implementation | Method |
|---|---|
| cpp | in process, encode and frame, mean of the repeats |
| python | in process, pack and unpack, mean of the repeats |
| rust | in process, mean of the repeats |
| zig | in process, mean of the repeats |

All four read the same fixture, defined by the same rules and proven equal by the canonical digest check.

## Lane metrics

| Lane | Impl | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|---|
| json | cpp | 42505784 | 405.731 | 537.521 |
| json | python | 42505756 | 2055.950 | 1859.869 |
| json | rust | 42505812 | 224.346 | 1011.816 |
| json | zig | 42372426 | 167.982 | 309.595 |
| ipc | cpp | 12670881 | 1.540 | 1.888 |
| ipc | python | 12670845 | 18.119 | 3.198 |
| ipc | rust | 12671117 | 1.582 | 2.808 |
| ipc | zig | 12670849 | 39.604 | 24.855 |
| ipc-zstd | cpp | 1608747 | 26.240 | 8.835 |
| ipc-zstd | python | 1620041 | 24.925 | 17.671 |
| ipc-zstd | rust | 1625309 | 25.428 | 9.813 |
| ipc-zstd | zig | 1620045 | 51.574 | 19.044 |

## Win conditions

```
LOSS ipc_encode_ms    cpp=1.540      rust=1.582      limit=1.424      others: python=18.119 zig=39.604
WIN  ipc_decode_ms    cpp=1.888      rust=2.808      limit=2.246      others: python=3.198 zig=24.855
WIN  json_encode_ms   cpp=405.731    python=2055.950   limit=2261.545   others: rust=224.346 zig=167.982
WIN  json_decode_ms   cpp=537.521    rust=1011.816   limit=607.090    others: python=1859.869 zig=309.595
WIN  ipc_wire_bytes   cpp=12670881   python=12670845   limit=12924262   others: rust=12671117 zig=12670849
```

Missed: ipc_encode_ms.

## Not measured by this harness

Startup time and peak RSS need a shell or a time utility inside the image. The Rust and Zig images are scratch images with neither, so those rows are unproven rather than won. The C++ numbers are in `docs/performance.md`.

## Image sizes

| Impl | Image bytes |
|---|---|
| cpp | 1258208946 |
| python | 408275466 |
| rust | 8621689 |
| zig | 2102909 |
