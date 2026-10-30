#include "harness.hpp"

#include "arrowlane/crc32.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace {
std::span<const std::byte> as_bytes(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}
}  // namespace

AL_TEST(crc32_known_vectors) {
    AL_CHECK_EQ(arrowlane::crc32({}), std::uint32_t{0});
    AL_CHECK_EQ(arrowlane::crc32(as_bytes("123456789")), std::uint32_t{0xCBF43926});
    AL_CHECK_EQ(arrowlane::crc32(as_bytes("The quick brown fox jumps over the lazy dog")),
                std::uint32_t{0x414FA339});

    const std::array<std::byte, 64> zeros{};
    AL_CHECK_EQ(arrowlane::crc32(zeros), std::uint32_t{0x758D6336});
}
