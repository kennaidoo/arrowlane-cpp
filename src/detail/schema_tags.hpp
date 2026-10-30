#pragma once

#include <cstdint>

namespace arrowlane::detail {

inline constexpr std::uint8_t kTypeNull = 1;
inline constexpr std::uint8_t kTypeInt = 2;
inline constexpr std::uint8_t kTypeFloating = 3;
inline constexpr std::uint8_t kTypeBinary = 4;
inline constexpr std::uint8_t kTypeUtf8 = 5;
inline constexpr std::uint8_t kTypeBool = 6;
inline constexpr std::uint8_t kTypeDecimal = 7;
inline constexpr std::uint8_t kTypeDate = 8;
inline constexpr std::uint8_t kTypeTime = 9;
inline constexpr std::uint8_t kTypeTimestamp = 10;
inline constexpr std::uint8_t kTypeFixedSizeBinary = 15;
inline constexpr std::uint8_t kTypeDuration = 18;
inline constexpr std::uint8_t kTypeLargeBinary = 19;
inline constexpr std::uint8_t kTypeLargeUtf8 = 20;

inline constexpr std::int16_t kMessageVersionV5 = 4;
inline constexpr std::uint8_t kMessageHeaderSchema = 1;
inline constexpr std::uint8_t kMessageHeaderRecordBatch = 3;

}  // namespace arrowlane::detail
