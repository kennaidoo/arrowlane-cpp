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

[[nodiscard]] Result<std::vector<std::byte>> encode_ipc(const RecordBatch& batch);

[[nodiscard]] Result<RecordBatch> decode_ipc(
    std::span<const std::byte> stream,
    std::size_t max_uncompressed = kMaxUncompressedBytes);

// Decodes without copying the body. `owner` must be the vector that `stream`
// points into and `base` the offset of `stream` inside it.
[[nodiscard]] Result<RecordBatch> decode_ipc_view(
    std::span<const std::byte> stream, std::shared_ptr<const std::vector<std::byte>> owner,
    std::size_t base, std::size_t max_uncompressed = kMaxUncompressedBytes);

}  // namespace arrowlane
