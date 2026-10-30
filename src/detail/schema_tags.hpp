#pragma once

#include <cstdint>

namespace arrowlane::detail {

inline constexpr std::int16_t kMessageVersionV5 = 4;
inline constexpr std::uint8_t kMessageHeaderSchema = 1;
inline constexpr std::uint8_t kMessageHeaderRecordBatch = 3;

}  // namespace arrowlane::detail
