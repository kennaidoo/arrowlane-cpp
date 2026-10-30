#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "arrowlane/column.hpp"
#include "arrowlane/error.hpp"
#include "arrowlane/frame.hpp"
#include "arrowlane/lanes.hpp"

namespace arrowlane {

inline constexpr std::size_t kDefaultChunkRows = 50'000;

// Preconditions for a chunk stream. A caller must check these before it builds
// a producer, because the producer constructor cannot fail.
[[nodiscard]] Result<void> validate_chunk_options(std::size_t rows_per_chunk,
                                                  std::size_t max_payload);

// Rows [start, end) of a batch. Fixed width values share the parent buffer;
// validity and offsets are copied because a bit offset and a rebased offset
// cannot be represented as a view.
[[nodiscard]] Result<RecordBatch> slice_batch(const RecordBatch& batch,
                                              std::int64_t start, std::int64_t end);

// Encodes one chunk per `next` call, so the consumer controls backpressure.
class ChunkProducer {
public:
    ChunkProducer(RecordBatch batch, LaneId lane, std::size_t rows_per_chunk,
                  std::size_t max_payload, LaneOptions options = {});

    // Encodes the next frame into `out`. Returns false when the stream is done
    // or when a frame failed; check `failed()`.
    [[nodiscard]] bool next(std::vector<std::byte>& out);

    [[nodiscard]] const std::string& stream_id() const noexcept { return stream_id_; }
    [[nodiscard]] std::uint64_t encoded_chunks() const noexcept { return encoded_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] Code failure_code() const noexcept { return failure_code_; }
    [[nodiscard]] const std::string& failure_message() const noexcept {
        return failure_message_;
    }

private:
    RecordBatch batch_;
    LaneId lane_;
    std::size_t rows_per_chunk_;
    std::size_t max_payload_;
    LaneOptions options_;
    std::string stream_id_;
    std::deque<std::pair<std::int64_t, std::int64_t>> ranges_;
    std::uint64_t index_ = 0;
    std::uint64_t encoded_ = 0;
    bool failed_ = false;
    Code failure_code_ = Code::Chunk;
    std::string failure_message_;
};

// Validates a chunk stream frame by frame and returns each decoded batch.
class ChunkAssembler {
public:
    explicit ChunkAssembler(std::size_t max_payload = kMaxPayloadBytes,
                            std::size_t max_uncompressed = kMaxUncompressedBytes);

    [[nodiscard]] Result<RecordBatch> add(std::span<const std::byte> frame_bytes);
    [[nodiscard]] bool finished() const noexcept { return saw_last_; }
    [[nodiscard]] std::uint64_t count() const noexcept { return next_index_; }
    [[nodiscard]] const std::string& stream_id() const noexcept { return stream_id_; }
    // Fails when the stream ended before a frame said `last`.
    [[nodiscard]] Result<void> check_complete() const;

private:
    std::size_t max_payload_;
    std::size_t max_uncompressed_;
    std::string stream_id_;
    std::optional<std::string> schema_key_;
    bool saw_last_ = false;
    std::uint64_t next_index_ = 0;
};

}  // namespace arrowlane
