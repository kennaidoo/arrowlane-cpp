#include "harness.hpp"

#include "arrowlane/crc32.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

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

// The hardware path must agree with the table path on every length, including
// every position relative to a 16 byte block, a 64 byte fold group, and the
// 128 byte switch to four accumulators.
AL_TEST(crc32_pclmul_matches_the_table) {
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    auto next = [&state]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    std::vector<std::byte> buffer(4096);
    std::size_t mismatches = 0;
    for (int case_index = 0; case_index < 10000; ++case_index) {
        const std::size_t size = static_cast<std::size_t>(next() % 4096) + 1;
        for (std::size_t index = 0; index < size; ++index) {
            buffer[index] = static_cast<std::byte>(next() & 0xFF);
        }
        const std::span<const std::byte> slice(buffer.data(), size);
        const auto table = arrowlane::crc32_table(slice);
        const auto fast = arrowlane::crc32_pclmul(slice);
        if (table != fast) {
            AL_CHECK_EQ(fast, table);
            ++mismatches;
            if (mismatches > 3) return;
        }
    }
    AL_CHECK_EQ(mismatches, std::size_t{0});
}

// The four known vectors must also survive the hardware path, including the
// buffer shorter than one block, which must fall back to the table.
AL_TEST(crc32_pclmul_matches_the_known_vectors) {
    AL_CHECK_EQ(arrowlane::crc32_pclmul({}), std::uint32_t{0});
    AL_CHECK_EQ(arrowlane::crc32_pclmul(as_bytes("123456789")),
                std::uint32_t{0xCBF43926});
    AL_CHECK_EQ(
        arrowlane::crc32_pclmul(as_bytes("The quick brown fox jumps over the lazy dog")),
        std::uint32_t{0x414FA339});
    const std::array<std::byte, 64> zeros{};
    AL_CHECK_EQ(arrowlane::crc32_pclmul(zeros), std::uint32_t{0x758D6336});
}
