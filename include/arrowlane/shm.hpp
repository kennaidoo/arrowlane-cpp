#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "arrowlane/error.hpp"

namespace arrowlane {

inline constexpr std::string_view kShmNamePrefix = "al-";
inline constexpr std::size_t kMaxSegmentBytes = 536'870'912;
inline constexpr std::size_t kMinTokenChars = 16;
inline constexpr double kDefaultSegmentTtlSeconds = 60.0;

// Where the payload is and how to prove it arrived intact. Every field is
// untrusted when it arrives from the wire.
struct ShmDescriptor {
    std::string name;
    std::uint64_t length = 0;
    std::uint32_t crc32 = 0;
    std::string token;
    std::uint64_t offset = 0;

    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] static Result<ShmDescriptor> from_json(std::string_view text);
};

// The directory that holds the segments. Empty means the default for this host.
[[nodiscard]] std::string segment_directory(const std::string& directory = {});

// True when this host can serve the shm lane. Always false on Windows.
[[nodiscard]] bool is_ready(const std::string& directory = {});

[[nodiscard]] Result<ShmDescriptor> write_segment(std::span<const std::byte> data,
                                                  const std::string& directory,
                                                  const std::string& token,
                                                  std::size_t max_attempts = 1024);

// Reads the whole segment into memory and removes the name. The token compare
// is constant time.
[[nodiscard]] Result<std::vector<std::byte>> read_segment(
    const ShmDescriptor& descriptor, const std::string& directory,
    std::string_view token, bool verify = true);

[[nodiscard]] Result<void> discard(const ShmDescriptor& descriptor,
                                   const std::string& directory);

// Removes segments older than the age. Returns how many names it removed.
[[nodiscard]] Result<std::size_t> sweep(
    const std::string& directory,
    double max_age_seconds = kDefaultSegmentTtlSeconds);

}  // namespace arrowlane
