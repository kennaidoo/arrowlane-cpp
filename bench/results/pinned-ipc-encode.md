# Four way comparison

Run 2026-10-31T10:25:17+02:00 on Windows-11-10.0.26300-SP0 with AMD64 Family 25 Model 33 Stepping 2, AuthenticAMD.
Rows 200000, repeats 15, rounds 9, implementations interleaved inside each round, each in its own container.

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
| json | cpp | 42505784 | 233.582 [225.812..244.680] | 562.951 [544.185..597.242] |
| json | rust | 42505812 | 245.528 [230.321..250.870] | 1121.238 [1063.500..1155.849] |
| ipc | cpp | 12670881 | 1.226 [1.110..1.573] | 1.392 [1.213..1.863] |
| ipc | rust | 12671117 | 1.439 [1.329..2.943] | 2.787 [2.636..3.727] |
| ipc-zstd | cpp | 1608747 | 27.425 [25.590..32.079] | 9.682 [8.721..11.567] |
| ipc-zstd | rust | 1625309 | 26.267 [24.665..30.454] | 10.388 [10.042..12.483] |

## Win conditions

```
UNDETERMINED  ipc_encode_ms    ratio=0.835 [0.403..1.064]  threshold=0.90  cpp=1.226 [1.110..1.573]  rust=1.439 [1.329..2.943]  others: 
WIN           ipc_decode_ms    ratio=0.484 [0.376..0.687]  threshold=0.80  cpp=1.392 [1.213..1.863]  rust=2.787 [2.636..3.727]  others: 
SKIPPED       json_encode_ms   needs python and cpp in --impls
WIN           json_decode_ms   ratio=0.507 [0.471..0.534]  threshold=0.60  cpp=562.951 [544.185..597.242]  rust=1121.238 [1063.500..1155.849]  others: 
SKIPPED       ipc_wire_bytes   needs python and cpp in --impls
```

Undetermined: ipc_encode_ms. Some round put C++ on the wrong side of the threshold, so the row is not settled by this run.

## Not measured by this harness

Startup time and peak RSS need a shell or a time utility inside the image. The Rust and Zig images are scratch images with neither, so those rows are unproven rather than won. The C++ numbers are in `docs/performance.md`.

## Image sizes

| Impl | Image bytes |
|---|---|
| cpp | 1258210111 |
| python | 408275466 |
| rust | 8621689 |
| zig | 2102909 |
