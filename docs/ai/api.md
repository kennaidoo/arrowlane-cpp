# API

The C++ surface, the C ABI, and the command line. Numbers are measured, never
estimated, and every one of them is reproducible with the command beside it.

## Frame

```cpp
#include "arrowlane/frame.hpp"

arrowlane::Result<std::vector<std::byte>> encode_frame(const arrowlane::Frame& frame);
arrowlane::Result<arrowlane::Frame> decode_frame(std::span<const std::byte> wire);
arrowlane::Result<arrowlane::FrameView> decode_frame_view(std::span<const std::byte> wire);
arrowlane::Result<std::size_t> frame_size(std::span<const std::byte> wire);
```

`decode_frame` copies the payload. `decode_frame_view` does not: it returns a
span into the caller buffer, and the caller keeps that buffer alive. Use the
view path when the payload is large and the caller already owns the bytes.

`frame_size` returns the length of the first frame in a buffer. A chunked file
is a run of frames, so a reader needs this to slice one out.

Limits: header 65,535 bytes, payload 536,870,912 bytes, uncompressed
1,073,741,824 bytes, 1,000,000 chunks, 10,000,000 rows per chunk.

## Lanes

```cpp
arrowlane::Result<arrowlane::Frame> encode_lane(LaneId lane, const RecordBatch& batch,
                                                const LaneOptions& options = {});
arrowlane::Result<arrowlane::RecordBatch> decode_lane(const Frame& frame,
                                                      const LaneOptions& options = {});
```

`LaneOptions` carries `level` for zstd, and `token` plus `directory` for shm.
The shm lane refuses to encode without a token of at least 16 characters.

## Selector

```cpp
auto model = arrowlane::CostModel::builtin();
auto decision = arrowlane::select_plan(workload, Topology::SameNetwork,
                                       Policy::Latency, *model);
```

The decision is deterministic. The same workload always produces the same lane,
reason, predicted time, predicted bytes, scores and rejections. The reasons are
`BEST`, `EMPTY`, `NON_TABULAR`, `NO_CANDIDATE` and `GAIN_TOO_SMALL`, and each
one names a rule in the reference implementation.

## Chunks

```cpp
arrowlane::ChunkProducer producer(batch, LaneId::Ipc, 50'000, arrowlane::kMaxPayloadBytes);
std::vector<std::byte> wire;
while (producer.next(wire)) { send(wire); }
```

`next` encodes exactly one frame, so a slow consumer stops the producer. A
chunk that exceeds the payload cap is split in half until it fits; a single row
that still does not fit is an `OversizeFrame` error.

## C ABI

`include/arrowlane/cabi.h` has the same shape as the Zig header, so one binding
drives either library.

```c
const char* al_version(void);
int al_select(uint64_t rows, uint64_t arrow_bytes, uint8_t topology,
              uint8_t policy, bool shm_ready, AlDecision* out);
AlBuffer al_inspect(const uint8_t* data, size_t len);
AlBuffer al_repack(const uint8_t* data, size_t len, uint8_t lane_id);
void al_free(AlBuffer buffer);
void al_free_reason(const char* reason);
```

Every entry point catches everything, including `std::bad_alloc`, and returns a
status or an empty buffer. `al_inspect` returns JSON with the lane, row count,
column count, wire bytes, uncompressed bytes and one CRC32 digest per column.

The `reason` field of `AlDecision` is a block the library allocates. Keep the
raw pointer and pass it to `al_free_reason`. Do not let a binding convert it to
a copy: freeing a copy corrupts the heap. This is not a theoretical hazard, the
ctypes check hit it.

## Command line

```text
arrowlane-cpp --version
arrowlane-cpp describe [--compact]
arrowlane-cpp select --rows N --bytes N [--topology T] [--policy P]
                     [--allow a,b] [--shm-ready] [--model FILE] [--json]
arrowlane-cpp pack --lane L [--rows N | --in FILE] --out FILE
                     [--chunks] [--rows-per-chunk N]
arrowlane-cpp unpack FILE [--json | --chunks]
arrowlane-cpp bench [--rows N] [--repeats N] [--json]
```

Exit codes: 0 success, 1 runtime error, 2 usage or config error. Errors print
one line to stderr as `error: <CodeName>: <message>`.

## Measured numbers

Release build, 2000 rows of the fixture, json lane, best of three repeats:

```bash
build-release/arrowlane-cpp.exe bench --rows 2000 --repeats 3 --json
```

| Implementation | encode | decode |
|---|---|---|
| arrowlane-cpp | 8.10 ms | 8.35 ms |
| arrowlane 0.3.1 (Python) | 15.45 ms | 11.33 ms |

That is 1.9 times faster on encode and 1.4 times on decode. It is not a claim
to be the fastest. The json lane allocates per row in both implementations, and
the comparison against Rust and Zig has not been run.
