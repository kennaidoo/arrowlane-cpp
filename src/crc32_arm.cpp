// CRC-32 over the ARMv8 CRC32 instructions.
//
// __crc32d consumes eight bytes and __crc32b one. The polynomial, the reflection
// and the complement convention are those of zlib's crc32, which the vector test
// in test/crc32_test.cpp pins: crc32("123456789") is 0xCBF43926.
//
// Compiled only for aarch64 with GCC or Clang. MSVC ARM64 declares these
// intrinsics in <arm64_neon.h>, which this file does not cover. Everywhere else
// the two functions fall through to crc32_table, so the translation unit still
// links on any architecture.

#include "arrowlane/crc32.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define AL_ARM_CRC 1
#include <arm_acle.h>
#else
#define AL_ARM_CRC 0
#endif

namespace arrowlane {

#if AL_ARM_CRC

namespace {

std::uint32_t crc32_arm_kernel(const std::uint8_t* data,
                               std::size_t size) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    while (size >= 8) {
        std::uint64_t word = 0;
        std::memcpy(&word, data, 8);  // the buffer is not required to be aligned
        crc = __crc32d(crc, word);
        data += 8;
        size -= 8;
    }
    while (size > 0) {
        crc = __crc32b(crc, *data);
        ++data;
        --size;
    }
    return ~crc;
}

}  // namespace

#endif  // AL_ARM_CRC

bool crc32_have_arm_crc32() noexcept {
#if !AL_ARM_CRC
    return false;
#else
    // The instructions are baseline on every aarch64 core this project targets,
    // so the compile time guard is the whole check. A build for a core without
    // them must pass -mno+crc, which turns AL_ARM_CRC off.
    return true;
#endif
}

std::uint32_t crc32_arm(std::span<const std::byte> bytes) noexcept {
#if !AL_ARM_CRC
    return crc32_table(bytes);
#else
    if (bytes.size() < 8 || !crc32_have_arm_crc32()) return crc32_table(bytes);
    return crc32_arm_kernel(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                            bytes.size());
#endif
}

}  // namespace arrowlane
