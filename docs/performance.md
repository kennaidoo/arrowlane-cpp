# Performance

Every number here is measured on one machine with one command. Nothing is
estimated and nothing is extrapolated. If a number is missing, the measurement
has not been done.

## Method

```bat
scripts\build.cmd Release
build-release\arrowlane-cpp.exe bench --rows 2000 --repeats 3 --json
```

Release build, mean of three repeats, one process, one thread. The table is the
eight column fixture: int64, utf8, float64, bool, date32, decimal128(9,2),
timestamp[us,UTC] and binary. Peak RSS comes from the operating system, not from
an estimate.

The machine is a desktop with a Ryzen 7 5800X3D, Windows 11, and no other load
worth naming. Run to run variation is about 25 percent, so treat differences
below that as noise.

## 2000 rows

| Lane | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| json | 416331 | 10.71 | 8.94 |
| ipc | 125113 | 0.24 | 0.15 |
| ipc-zstd | 21373 | 0.53 | 0.24 |

Peak RSS for the whole bench process: 6528 KiB.

The ipc lane is 44 times faster than json on encode and 60 times on decode,
because json writes a text line per row and allocates per value. The zstd lane
pays about 0.3 ms to cut the payload to a sixth.

## Against the Python reference

Same table, same machine, same day, mean of three repeats. The Python side is
CPython 3.15 with pyarrow 26.0.0, using `arrowlane.transit.pack` and `unpack`.

| Implementation | Encode ms | Decode ms |
|---|---|---|
| arrowlane-cpp | 10.71 | 8.94 |
| arrowlane 0.3.1 | 24.94 | 12.18 |

That is 2.3 times on encode and 1.4 times on decode. It is not a claim to be the
fastest, and it is not a full comparison: the Rust and Zig implementations have
not been measured on this workload. Phase 10 owns that work, with larger
payloads, more repeats and a recorded win or loss per lane.

## Binary size

| Artifact | Bytes |
|---|---|
| arrowlane-cpp.exe, release | 1122816 |
| arrowlane_capi.dll, release | 1000448 |

Both are single files with no runtime dependency beyond the C++ runtime, and
zstd is linked statically.
