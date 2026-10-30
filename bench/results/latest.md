# Four way comparison

Run 2026-10-30T22:26:53+02:00 on Windows-11-10.0.26300-SP0 with AMD64 Family 25 Model 33 Stepping 2, AuthenticAMD.
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
| json | cpp | 42505784 | 418.034 | 594.791 |
| json | python | 42505756 | 2212.442 | 2076.506 |
| json | rust | 42505812 | 252.245 | 1182.569 |
| json | zig | 42372426 | 195.108 | 352.970 |
| ipc | cpp | 12670881 | 3.449 | 2.640 |
| ipc | python | 12670845 | 21.601 | 3.785 |
| ipc | rust | 12671117 | 2.656 | 3.490 |
| ipc | zig | 12670849 | 46.989 | 26.738 |
| ipc-zstd | cpp | 1608747 | 28.455 | 10.144 |
| ipc-zstd | python | 1620041 | 31.133 | 26.165 |
| ipc-zstd | rust | 1625309 | 30.259 | 11.885 |
| ipc-zstd | zig | 1620045 | 65.474 | 22.717 |

## Win conditions

```
LOSS ipc_encode_ms    cpp=3.449      rust=2.656      limit=2.390      others: python=21.601 zig=46.989
WIN  ipc_decode_ms    cpp=2.640      rust=3.490      limit=2.792      others: python=3.785 zig=26.738
WIN  json_encode_ms   cpp=418.034    python=2212.442   limit=2433.686   others: rust=252.245 zig=195.108
WIN  json_decode_ms   cpp=594.791    rust=1182.569   limit=709.541    others: python=2076.506 zig=352.970
WIN  ipc_wire_bytes   cpp=12670881   python=12670845   limit=12924262   others: rust=12671117 zig=12670849
```

Missed: ipc_encode_ms.

## Not measured by this harness

Startup time and peak RSS need a shell or a time utility inside the image. The Rust and Zig images are scratch images with neither, so those rows are unproven rather than won. The C++ numbers are in `docs/performance.md`.

## Image sizes

| Impl | Image bytes |
|---|---|
| cpp | 1258199689 |
| python | 408275466 |
| rust | 8621689 |
| zig | 2102909 |
