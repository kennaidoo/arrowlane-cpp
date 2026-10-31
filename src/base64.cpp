#include "arrowlane/base64.hpp"

#include <cstdint>

namespace arrowlane {
namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decode_char(char value) noexcept {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

}  // namespace

void base64_append(std::string& out, std::span<const std::byte> bytes) {
    std::size_t index = 0;
    for (; index + 3 <= bytes.size(); index += 3) {
        const std::uint32_t block = (std::uint32_t(bytes[index]) << 16) |
                                    (std::uint32_t(bytes[index + 1]) << 8) |
                                    std::uint32_t(bytes[index + 2]);
        out.push_back(kAlphabet[(block >> 18) & 63]);
        out.push_back(kAlphabet[(block >> 12) & 63]);
        out.push_back(kAlphabet[(block >> 6) & 63]);
        out.push_back(kAlphabet[block & 63]);
    }
    const std::size_t rest = bytes.size() - index;
    if (rest == 1) {
        const std::uint32_t block = std::uint32_t(bytes[index]) << 16;
        out.push_back(kAlphabet[(block >> 18) & 63]);
        out.push_back(kAlphabet[(block >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (rest == 2) {
        const std::uint32_t block =
            (std::uint32_t(bytes[index]) << 16) | (std::uint32_t(bytes[index + 1]) << 8);
        out.push_back(kAlphabet[(block >> 18) & 63]);
        out.push_back(kAlphabet[(block >> 12) & 63]);
        out.push_back(kAlphabet[(block >> 6) & 63]);
        out.push_back('=');
    }
}

std::string base64_encode(std::span<const std::byte> bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    base64_append(out, bytes);
    return out;
}

Result<std::vector<std::byte>> base64_decode(std::string_view text) {
    if (text.size() % 4 != 0) {
        return std::unexpected(
            make_error(Code::BadHeader, "base64 length is not a multiple of 4"));
    }
    std::vector<std::byte> out;
    out.reserve((text.size() / 4) * 3);
    for (std::size_t index = 0; index < text.size(); index += 4) {
        const bool pad2 = text[index + 2] == '=';
        const bool pad3 = text[index + 3] == '=';
        const int a = decode_char(text[index]);
        const int b = decode_char(text[index + 1]);
        const int c = pad2 ? 0 : decode_char(text[index + 2]);
        const int d = pad3 ? 0 : decode_char(text[index + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) {
            return std::unexpected(
                make_error(Code::BadHeader, "base64 has an invalid character"));
        }
        if (pad2 && !pad3) {
            return std::unexpected(make_error(Code::BadHeader, "base64 padding is invalid"));
        }
        if ((pad2 || pad3) && index + 4 != text.size()) {
            return std::unexpected(
                make_error(Code::BadHeader, "base64 padding is not at the end"));
        }
        const std::uint32_t block = (std::uint32_t(a) << 18) | (std::uint32_t(b) << 12) |
                                    (std::uint32_t(c) << 6) | std::uint32_t(d);
        out.push_back(std::byte((block >> 16) & 0xFF));
        if (!pad2) out.push_back(std::byte((block >> 8) & 0xFF));
        if (!pad3) out.push_back(std::byte(block & 0xFF));
    }
    return out;
}

}  // namespace arrowlane
