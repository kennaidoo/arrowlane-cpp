# Four way comparison

Run 2026-10-31T09:35:24+02:00 on Windows-11-10.0.26300-SP0 with AMD64 Family 25 Model 33 Stepping 2, AuthenticAMD.
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
| json | cpp | 42505784 | 420.511 [419.206..427.057] | 590.546 [550.509..593.088] |
| json | python | 42505756 | 2149.249 [2057.714..2247.899] | 2052.513 [1973.270..2079.816] |
| json | rust | 42505812 | 262.011 [248.045..272.111] | 1204.029 [1174.650..1237.234] |
| json | zig | 42372426 | 180.133 [177.324..185.798] | 339.865 [329.518..348.563] |
| ipc | cpp | 12670881 | 1.306 [1.097..1.480] | 1.399 [1.277..1.582] |
| ipc | python | 12670845 | 19.272 [18.771..19.640] | 3.436 [3.296..3.855] |
| ipc | rust | 12671117 | 1.883 [1.785..1.987] | 2.741 [2.739..2.831] |
| ipc | zig | 12670849 | 43.930 [42.961..45.679] | 26.259 [26.017..27.067] |
| ipc-zstd | cpp | 1608747 | 28.107 [25.894..31.658] | 9.475 [9.116..10.684] |
| ipc-zstd | python | 1620041 | 27.977 [26.209..30.320] | 20.001 [19.142..21.079] |
| ipc-zstd | rust | 1625309 | 30.646 [26.199..31.082] | 11.486 [10.736..12.402] |
| ipc-zstd | zig | 1620045 | 62.386 [60.291..66.337] | 20.964 [20.697..23.807] |

## Win conditions

```
WIN           ipc_encode_ms    cpp=1.306 [1.097..1.480]  rust=1.883 [1.785..1.987]  limit=1.695  overlap=no  others: python=19.272 zig=43.930
WIN           ipc_decode_ms    cpp=1.399 [1.277..1.582]  rust=2.741 [2.739..2.831]  limit=2.193  overlap=no  others: python=3.436 zig=26.259
WIN           json_encode_ms   cpp=420.511 [419.206..427.057]  python=2149.249 [2057.714..2247.899]  limit=2364.174  overlap=no  others: rust=262.011 zig=180.133
WIN           json_decode_ms   cpp=590.546 [550.509..593.088]  rust=1204.029 [1174.650..1237.234]  limit=722.417  overlap=no  others: python=2052.513 zig=339.865
WIN           ipc_wire_bytes   cpp=12670881 [12670881..12670881]  python=12670845 [12670845..12670845]  limit=12924262  overlap=no  others: rust=12671117 zig=12670849
```

ALL WIN CONDITIONS MET

## Not measured by this harness

Startup time and peak RSS need a shell or a time utility inside the image. The Rust and Zig images are scratch images with neither, so those rows are unproven rather than won. The C++ numbers are in `docs/performance.md`.

## Image sizes

| Impl | Image bytes |
|---|---|
| cpp | 1258209513 |
| python | 408275466 |
| rust | 8621689 |
| zig | 2102909 |
