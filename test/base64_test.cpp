#include "harness.hpp"

#include "arrowlane/base64.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace {
std::span<const std::byte> as_bytes(const char* text) {
    return {reinterpret_cast<const std::byte*>(text), std::char_traits<char>::length(text)};
}
}  // namespace

AL_TEST(base64_known_vectors) {
    AL_CHECK_EQ(arrowlane::base64_encode(as_bytes("")), std::string(""));
    AL_CHECK_EQ(arrowlane::base64_encode(as_bytes("f")), std::string("Zg=="));
    AL_CHECK_EQ(arrowlane::base64_encode(as_bytes("fo")), std::string("Zm8="));
    AL_CHECK_EQ(arrowlane::base64_encode(as_bytes("foo")), std::string("Zm9v"));
    AL_CHECK_EQ(arrowlane::base64_encode(as_bytes("foob")), std::string("Zm9vYg=="));
}

AL_TEST(base64_roundtrip) {
    const std::array<std::byte, 5> input{std::byte{0x00}, std::byte{0x01}, std::byte{0xFE},
                                         std::byte{0xFF}, std::byte{0x7F}};
    const auto encoded = arrowlane::base64_encode(input);
    auto decoded = arrowlane::base64_decode(encoded);
    AL_REQUIRE(decoded.has_value());
    AL_CHECK_EQ(decoded->size(), input.size());
    AL_CHECK_EQ((*decoded)[2], std::byte{0xFE});
}

AL_TEST(base64_rejects_bad_input) {
    AL_CHECK_ERR(arrowlane::base64_decode("A"), arrowlane::Code::BadHeader);
    AL_CHECK_ERR(arrowlane::base64_decode("AA=A"), arrowlane::Code::BadHeader);
    AL_CHECK_ERR(arrowlane::base64_decode("AA!A"), arrowlane::Code::BadHeader);
}
