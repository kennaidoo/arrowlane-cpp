#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "arrowlane/error.hpp"

namespace arrowlane::detail {

struct Decimal128 {
    std::array<std::uint32_t, 4> limbs{};  // little endian
    bool negative = false;
};

[[nodiscard]] Decimal128 decimal128_from_bytes(
    std::span<const std::byte, 16> bytes) noexcept;
[[nodiscard]] std::array<std::byte, 16> decimal128_to_bytes(
    const Decimal128& value) noexcept;
[[nodiscard]] std::string decimal128_to_text(std::span<const std::byte, 16> bytes,
                                             std::int32_t scale);
[[nodiscard]] Result<std::array<std::byte, 16>> decimal128_from_text(
    std::string_view text, std::int32_t scale);

}  // namespace arrowlane::detail
