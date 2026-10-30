#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace arrowlane {

[[nodiscard]] std::uint32_t crc32(std::span<const std::byte> bytes) noexcept;

// Slicing by eight. Never disabled: it is the fallback, and it is the oracle
// the hardware path is tested against.
[[nodiscard]] std::uint32_t crc32_table(std::span<const std::byte> bytes) noexcept;

// Carry-less multiply path. Returns exactly what crc32_table returns, on any
// CPU: when the instructions or the length are missing it calls crc32_table.
[[nodiscard]] std::uint32_t crc32_pclmul(std::span<const std::byte> bytes) noexcept;

// True when this CPU reports PCLMULQDQ, SSSE3 and SSE4.1 on leaf 1.
[[nodiscard]] bool crc32_have_pclmul() noexcept;

}  // namespace arrowlane
