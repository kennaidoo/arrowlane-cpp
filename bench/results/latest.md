# Four way comparison

Run 2026-10-31T09:55:27+02:00 on Windows-11-10.0.26300-SP0 with AMD64 Family 25 Model 33 Stepping 2, AuthenticAMD.
Rows 200000, repeats 7, rounds 3, implementations interleaved inside each round, each in its own container.

## Method

| Implementation | Method |
|---|---|
| cpp | in process, lane encode plus frame, median of the repeats |
| python | in process, pack then unpack, median of the repeats |
| rust | in process, median of the repeats |
| zig | in process, median of the repeats |

All four read the same fixture, defined by the same rules and proven equal by the canonical digest check.

## Lane metrics

Median of the rounds, with the range in brackets.

| Lane | Impl | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|---|
| json | cpp | 42505784 | 246.668 [243.798..254.676] | 589.337 [586.845..592.738] |
| json | python | 42505756 | 2290.481 [2249.773..2300.864] | 2157.549 [2103.980..2171.663] |
| json | rust | 42505812 | 267.012 [252.902..272.253] | 1171.603 [1164.418..1234.781] |
| json | zig | 42372426 | 190.893 [190.671..193.992] | 351.893 [335.849..356.158] |
| ipc | cpp | 12670881 | 1.847 [1.550..2.187] | 1.786 [1.400..2.163] |
| ipc | python | 12670845 | 22.859 [22.052..22.893] | 3.580 [3.399..4.958] |
| ipc | rust | 12671117 | 1.926 [1.755..2.252] | 2.844 [2.807..2.868] |
| ipc | zig | 12670849 | 45.428 [45.219..45.429] | 27.501 [26.166..28.196] |
| ipc-zstd | cpp | 1608747 | 29.806 [28.912..30.943] | 10.257 [9.393..11.923] |
| ipc-zstd | python | 1620041 | 32.119 [29.884..32.818] | 22.097 [21.200..24.189] |
| ipc-zstd | rust | 1625309 | 28.465 [27.107..30.457] | 11.165 [10.728..12.033] |
| ipc-zstd | zig | 1620045 | 64.609 [63.422..66.830] | 22.682 [22.590..24.746] |

## Win conditions

```
UNDETERMINED  ipc_encode_ms    cpp=1.847 [1.550..2.187]  rust=1.926 [1.755..2.252]  limit=1.733  overlap=yes  others: python=22.859 zig=45.428
WIN           ipc_decode_ms    cpp=1.786 [1.400..2.163]  rust=2.844 [2.807..2.868]  limit=2.275  overlap=no  others: python=3.580 zig=27.501
WIN           json_encode_ms   cpp=246.668 [243.798..254.676]  python=2290.481 [2249.773..2300.864]  limit=2519.529  overlap=no  others: rust=267.012 zig=190.893
WIN           json_decode_ms   cpp=589.337 [586.845..592.738]  rust=1171.603 [1164.418..1234.781]  limit=702.962  overlap=no  others: python=2157.549 zig=351.893
WIN           ipc_wire_bytes   cpp=12670881 [12670881..12670881]  python=12670845 [12670845..12670845]  limit=12924262  overlap=no  others: rust=12671117 zig=12670849
```

Undetermined: ipc_encode_ms. The C++ range and the reference range overlap, so no single run settles these rows.

## Not measured by this harness

Startup time and peak RSS need a shell or a time utility inside the image. The Rust and Zig images are scratch images with neither, so those rows are unproven rather than won. The C++ numbers are in `docs/performance.md`.

## Image sizes

| Impl | Image bytes |
|---|---|
| cpp | 1258210111 |
| python | 408275466 |
| rust | 8621689 |
| zig | 2102909 |
