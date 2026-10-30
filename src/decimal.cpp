#include "detail/decimal.hpp"

#include <algorithm>
#include <cassert>

#include "detail/bytes.hpp"

namespace arrowlane::detail {
namespace {

bool limbs_zero(const std::array<std::uint32_t, 4>& limbs) noexcept {
    return (limbs[0] | limbs[1] | limbs[2] | limbs[3]) == 0;
}

void negate(std::array<std::uint32_t, 4>& limbs) noexcept {
    std::uint64_t carry = 1;
    for (std::size_t index = 0; index < 4; ++index) {
        const std::uint64_t current =
            (static_cast<std::uint64_t>(~limbs[index] & 0xFFFFFFFFu)) + carry;
        limbs[index] = static_cast<std::uint32_t>(current);
        carry = current >> 32;
    }
}

bool mul_add(std::array<std::uint32_t, 4>& limbs, std::uint64_t digit) noexcept {
    std::uint64_t carry = digit;
    for (std::size_t index = 0; index < 4; ++index) {
        const std::uint64_t current =
            static_cast<std::uint64_t>(limbs[index]) * 10 + carry;
        limbs[index] = static_cast<std::uint32_t>(current);
        carry = current >> 32;
    }
    return carry == 0;
}

}  // namespace

Decimal128 decimal128_from_bytes(std::span<const std::byte, 16> bytes) noexcept {
    Decimal128 value;
    for (std::size_t index = 0; index < 4; ++index) {
        value.limbs[index] = load_le<std::uint32_t>(bytes.data() + 4 * index);
    }
    value.negative = (value.limbs[3] >> 31) != 0;
    return value;
}

std::array<std::byte, 16> decimal128_to_bytes(const Decimal128& value) noexcept {
    std::array<std::byte, 16> out{};
    for (std::size_t index = 0; index < 4; ++index) {
        store_le<std::uint32_t>(out.data() + 4 * index, value.limbs[index]);
    }
    return out;
}

std::string decimal128_to_text(std::span<const std::byte, 16> bytes,
                               std::int32_t scale) {
    assert(scale >= 0 && scale <= 38);
    const Decimal128 value = decimal128_from_bytes(bytes);
    const bool negative = value.negative;
    std::array<std::uint32_t, 4> limbs = value.limbs;
    if (negative) negate(limbs);
    const bool zero = limbs_zero(limbs);

    std::string digits;
    while (!limbs_zero(limbs)) {
        std::uint64_t remainder = 0;
        for (int i = 3; i >= 0; --i) {
            const std::uint64_t current =
                (remainder << 32) | limbs[static_cast<std::size_t>(i)];
            limbs[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(current / 10);
            remainder = current % 10;
        }
        digits.push_back(static_cast<char>('0' + remainder));
    }
    if (digits.empty()) digits.push_back('0');
    std::reverse(digits.begin(), digits.end());

    std::string out;
    if (negative && !zero) out.push_back('-');
    if (scale == 0) {
        out += digits;
        return out;
    }
    const auto places = static_cast<std::size_t>(scale);
    if (digits.size() <= places) {
        out += "0.";
        out.append(places - digits.size(), '0');
        out += digits;
    } else {
        out += digits.substr(0, digits.size() - places);
        out.push_back('.');
        out += digits.substr(digits.size() - places);
    }
    return out;
}

Result<std::array<std::byte, 16>> decimal128_from_text(std::string_view text,
                                                      std::int32_t scale) {
    if (scale < 0 || scale > 38) {
        return std::unexpected(make_error(Code::BadHeader,
                                          "decimal scale must be 0..38"));
    }
    std::size_t pos = 0;
    bool negative = false;
    if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
        negative = text[pos] == '-';
        ++pos;
    }
    std::array<std::uint32_t, 4> limbs{};
    std::size_t digit_count = 0;
    std::size_t fraction = 0;
    bool seen_point = false;
    for (; pos < text.size(); ++pos) {
        const char symbol = text[pos];
        if (symbol == '.') {
            if (seen_point) {
                return std::unexpected(make_error(
                    Code::BadHeader, "decimal text has more than one point"));
            }
            seen_point = true;
            continue;
        }
        if (symbol < '0' || symbol > '9') {
            return std::unexpected(
                make_error(Code::BadHeader, "decimal text carries a non digit"));
        }
        if (!mul_add(limbs, static_cast<std::uint64_t>(symbol - '0'))) {
            return std::unexpected(make_error(Code::BadHeader,
                                              "decimal text overflows 128 bits"));
        }
        ++digit_count;
        if (seen_point) ++fraction;
        if (fraction > static_cast<std::size_t>(scale)) {
            return std::unexpected(make_error(
                Code::BadHeader,
                "decimal text has more fraction digits than the scale"));
        }
    }
    if (digit_count == 0) {
        return std::unexpected(
            make_error(Code::BadHeader, "decimal text has no digits"));
    }
    for (std::size_t step = fraction; step < static_cast<std::size_t>(scale); ++step) {
        if (!mul_add(limbs, 0)) {
            return std::unexpected(make_error(Code::BadHeader,
                                              "decimal text overflows 128 bits"));
        }
    }
    if (!negative) {
        if (limbs[3] >= 0x80000000u) {
            return std::unexpected(make_error(
                Code::BadHeader, "decimal text exceeds the decimal128 range"));
        }
    } else {
        const bool over_limit = limbs[3] > 0x80000000u ||
                                (limbs[3] == 0x80000000u &&
                                 (limbs[0] | limbs[1] | limbs[2]) != 0);
        if (over_limit) {
            return std::unexpected(make_error(
                Code::BadHeader, "decimal text is below the decimal128 range"));
        }
        negate(limbs);
    }
    Decimal128 value;
    value.limbs = limbs;
    return decimal128_to_bytes(value);
}

}  // namespace arrowlane::detail
