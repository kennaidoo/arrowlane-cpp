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

// Eight stream path over 256 bit carry-less multiplies. Returns exactly what
// crc32_table returns, on any CPU: when the instructions, the operating system
// state or the length are missing it falls back to crc32_pclmul.
[[nodiscard]] std::uint32_t crc32_wide(std::span<const std::byte> bytes) noexcept;

// True when this CPU reports AVX2 and VPCLMULQDQ on leaf 7 and the operating
// system has enabled the YMM state.
[[nodiscard]] bool crc32_have_vpclmul() noexcept;

// ARMv8 CRC32 instruction path. Returns exactly what crc32_table returns, on any
// CPU: when the instructions or the length are missing it calls crc32_table.
[[nodiscard]] std::uint32_t crc32_arm(std::span<const std::byte> bytes) noexcept;

// True on aarch64 when the CRC32 instructions are available at compile time.
[[nodiscard]] bool crc32_have_arm_crc32() noexcept;

}  // namespace arrowlane
