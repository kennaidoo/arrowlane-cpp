#include "harness.hpp"

#include "detail/bytes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

AL_TEST(load_store_roundtrip) {
    std::array<std::byte, 8> buffer{};
    arrowlane::detail::store_le<std::uint64_t>(buffer.data(), 0x0102030405060708ULL);
    AL_CHECK_EQ(buffer[0], std::byte{0x08});
    AL_CHECK_EQ(buffer[7], std::byte{0x01});
    AL_CHECK_EQ(arrowlane::detail::load_le<std::uint64_t>(buffer.data()),
                0x0102030405060708ULL);
}
