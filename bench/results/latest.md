# Four way comparison

Run 2026-10-30T20:35:29+02:00 on Windows-11-10.0.26300-SP0 with AMD64 Family 25 Model 33 Stepping 2, AuthenticAMD.
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
| json | cpp | 42505784 | 515.954 | 536.766 |
| json | python | 42505756 | 1965.868 | 1871.103 |
| json | rust | 42505812 | 223.832 | 1051.174 |
| json | zig | 42372426 | 169.826 | 313.475 |
| ipc | cpp | 12670881 | 6.996 | 5.935 |
| ipc | python | 12670845 | 18.199 | 3.365 |
| ipc | rust | 12671117 | 1.387 | 2.421 |
| ipc | zig | 12670849 | 40.296 | 24.687 |
| ipc-zstd | cpp | 1608747 | 23.853 | 8.417 |
| ipc-zstd | python | 1620041 | 25.030 | 18.080 |
| ipc-zstd | rust | 1625309 | 24.252 | 9.889 |
| ipc-zstd | zig | 1620045 | 54.904 | 19.964 |

## Win conditions

```
LOSS ipc_encode_ms    cpp=6.996      rust=1.387      limit=1.248      others: python=18.199 zig=40.296
LOSS ipc_decode_ms    cpp=5.935      rust=2.421      limit=1.937      others: python=3.365 zig=24.687
WIN  json_encode_ms   cpp=515.954    python=1965.868   limit=2162.455   others: rust=223.832 zig=169.826
WIN  json_decode_ms   cpp=536.766    rust=1051.174   limit=630.704    others: python=1871.103 zig=313.475
WIN  ipc_wire_bytes   cpp=12670881   python=12670845   limit=12924262   others: rust=12671117 zig=12670849
```

Missed: ipc_encode_ms, ipc_decode_ms.

## Not measured by this harness

Startup time and peak RSS need a shell or a time utility inside the image. The Rust and Zig images are scratch images with neither, so those rows are unproven rather than won. The C++ numbers are in `docs/performance.md`.

## Image sizes

| Impl | Image bytes |
|---|---|
| cpp | 1258190012 |
| python | 408275466 |
| rust | 8621689 |
| zig | 2102909 |
