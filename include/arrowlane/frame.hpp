#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "arrowlane/error.hpp"
#include "arrowlane/json.hpp"

namespace arrowlane {

inline constexpr std::size_t kPrefixSize = 38;
inline constexpr std::uint8_t kFormatVersion = 1;
inline constexpr std::size_t kMaxHeaderBytes = 65'535;
inline constexpr std::size_t kMaxPayloadBytes = 536'870'912;
inline constexpr std::size_t kMaxUncompressedBytes = 1'073'741'824;
inline constexpr std::uint64_t kMaxChunks = 1'000'000;
inline constexpr std::size_t kMaxChunkRows = 10'000'000;

enum class LaneId : std::uint8_t { Json = 1, Ipc = 2, IpcZstd = 3, Shm = 4 };

[[nodiscard]] std::optional<LaneId> lane_id_from_name(std::string_view name) noexcept;
[[nodiscard]] std::string_view lane_name(LaneId lane) noexcept;

struct ChunkInfo {
    std::string id;
    std::uint64_t index = 0;
    bool last = false;
};

[[nodiscard]] Result<void> validate_chunk(const ChunkInfo& chunk);
[[nodiscard]] Result<ChunkInfo> parse_chunk(const JsonValue& value);

struct Frame {
    LaneId lane = LaneId::Json;
    std::vector<std::byte> payload;
    std::uint64_t rows = 0;
    std::uint64_t uncompressed_len = 0;
    std::uint16_t flags = 0;
    JsonValue header;

    [[nodiscard]] std::optional<ChunkInfo> chunk() const;
};

[[nodiscard]] Result<std::vector<std::byte>> encode_frame(
    const Frame& frame, std::size_t max_payload = kMaxPayloadBytes);

[[nodiscard]] Result<Frame> decode_frame(
    std::span<const std::byte> data, std::size_t max_payload = kMaxPayloadBytes,
    std::size_t max_uncompressed = kMaxUncompressedBytes);

struct FrameParts {
    std::array<std::byte, kPrefixSize> prefix{};
    std::string header;
    std::span<const std::byte> payload;
};

[[nodiscard]] Result<FrameParts> encode_parts(
    const Frame& frame, std::size_t max_payload = kMaxPayloadBytes);

[[nodiscard]] Result<std::uint64_t> write_frame(
    std::ostream& stream, const Frame& frame, std::size_t max_payload = kMaxPayloadBytes);

[[nodiscard]] Result<std::optional<Frame>> read_frame(
    std::istream& stream, std::size_t max_payload = kMaxPayloadBytes,
    std::size_t max_uncompressed = kMaxUncompressedBytes);

}  // namespace arrowlane
