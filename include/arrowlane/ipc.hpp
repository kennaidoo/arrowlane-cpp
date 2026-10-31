#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "arrowlane/column.hpp"
#include "arrowlane/error.hpp"
#include "arrowlane/frame.hpp"

namespace arrowlane {

struct IpcBuffer {
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

struct IpcLayout {
    std::vector<IpcBuffer> buffers;
    std::vector<std::pair<std::int64_t, std::int64_t>> nodes;  // length, null_count
    std::uint64_t body_size = 0;
};

[[nodiscard]] Result<IpcLayout> plan_ipc_body(const RecordBatch& batch);

[[nodiscard]] Result<std::vector<std::byte>> build_ipc_body(const RecordBatch& batch,
                                                            const IpcLayout& layout);

// `headroom` bytes are left zeroed at the front of the result, so a lane can
// hand the buffer to encode_frame without another copy.
[[nodiscard]] Result<std::vector<std::byte>> encode_ipc(const RecordBatch& batch,
                                                        std::size_t headroom = 0);

// Writes the stream into `buffer` and returns the part of it that holds the
// stream. The size is planned before anything is written, so a buffer that is too
// small is refused without a partial write. A caller that keeps one buffer and
// reuses it pays no page faults for the payload, which is where more than half of
// the Windows encode time goes.
[[nodiscard]] Result<std::span<std::byte>> encode_ipc_into(const RecordBatch& batch,
                                                           std::span<std::byte> buffer);

[[nodiscard]] Result<RecordBatch> decode_ipc(
    std::span<const std::byte> stream,
    std::size_t max_uncompressed = kMaxUncompressedBytes);

// Decodes without copying the body. `owner` must be the vector that `stream`
// points into and `base` the offset of `stream` inside it.
[[nodiscard]] Result<RecordBatch> decode_ipc_view(
    std::span<const std::byte> stream, std::shared_ptr<const std::vector<std::byte>> owner,
    std::size_t base, std::size_t max_uncompressed = kMaxUncompressedBytes);

}  // namespace arrowlane
