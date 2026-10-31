#include "harness.hpp"

#include "detail/decimal.hpp"

#include <string>

namespace {

std::array<std::byte, 16> bytes_of(std::uint32_t limb0, std::uint32_t limb1,
                                   std::uint32_t limb2, std::uint32_t limb3) {
    arrowlane::detail::Decimal128 value;
    value.limbs = {limb0, limb1, limb2, limb3};
    return arrowlane::detail::decimal128_to_bytes(value);
}

}  // namespace

AL_TEST(decimal_text_format) {
    AL_CHECK_EQ(arrowlane::detail::decimal128_to_text(bytes_of(1234, 0, 0, 0), 2),
                std::string("12.34"));
    AL_CHECK_EQ(arrowlane::detail::decimal128_to_text(bytes_of(5, 0, 0, 0), 3),
                std::string("0.005"));
    AL_CHECK_EQ(arrowlane::detail::decimal128_to_text(bytes_of(120, 0, 0, 0), 1),
                std::string("12.0"));
    AL_CHECK_EQ(arrowlane::detail::decimal128_to_text(bytes_of(0, 0, 0, 0), 2),
                std::string("0.00"));
}

AL_TEST(decimal_negative_and_roundtrip) {
    auto parsed = arrowlane::detail::decimal128_from_text("-5.00", 2);
    AL_REQUIRE(parsed.has_value());
    AL_CHECK_EQ(arrowlane::detail::decimal128_to_text(*parsed, 2), std::string("-5.00"));

    auto again = arrowlane::detail::decimal128_from_text("12.34", 2);
    AL_REQUIRE(again.has_value());
    AL_CHECK_EQ(arrowlane::detail::decimal128_to_text(*again, 2), std::string("12.34"));
}

AL_TEST(decimal_rejects_bad_text) {
    AL_CHECK_ERR(arrowlane::detail::decimal128_from_text("1e3", 2),
                 arrowlane::Code::BadHeader);
    AL_CHECK_ERR(arrowlane::detail::decimal128_from_text("12.345", 2),
                 arrowlane::Code::BadHeader);
    AL_CHECK_ERR(arrowlane::detail::decimal128_from_text("", 2),
                 arrowlane::Code::BadHeader);
    AL_CHECK_ERR(arrowlane::detail::decimal128_from_text("1.2.3", 2),
                 arrowlane::Code::BadHeader);
    AL_CHECK_ERR(
        arrowlane::detail::decimal128_from_text(
            "999999999999999999999999999999999999999", 0),
        arrowlane::Code::BadHeader);
}

AL_TEST(decimal_bytes_and_limits) {
    auto minimum = arrowlane::detail::decimal128_from_text(
        "-170141183460469231731687303715884105728", 0);
    AL_REQUIRE(minimum.has_value());
    AL_CHECK_EQ(arrowlane::detail::decimal128_to_text(*minimum, 0),
                std::string("-170141183460469231731687303715884105728"));

    auto maximum = arrowlane::detail::decimal128_from_text(
        "170141183460469231731687303715884105727", 0);
    AL_REQUIRE(maximum.has_value());
    AL_CHECK_EQ(arrowlane::detail::decimal128_to_text(*maximum, 0),
                std::string("170141183460469231731687303715884105727"));

    auto negative = arrowlane::detail::decimal128_from_text("-5", 0);
    AL_REQUIRE(negative.has_value());
    const auto negative_bytes = *negative;
    const auto negative_again = arrowlane::detail::decimal128_from_bytes(negative_bytes);
    AL_CHECK(negative_again.negative);
    AL_CHECK_EQ(arrowlane::detail::decimal128_to_text(negative_bytes, 0),
                std::string("-5"));

    auto integer = arrowlane::detail::decimal128_from_text("1234", 0);
    AL_REQUIRE(integer.has_value());
    AL_CHECK_EQ(arrowlane::detail::decimal128_to_text(*integer, 0),
                std::string("1234"));

    AL_CHECK_ERR(
        arrowlane::detail::decimal128_from_text(
            "-170141183460469231731687303715884105729", 0),
        arrowlane::Code::BadHeader);
    AL_CHECK_ERR(
        arrowlane::detail::decimal128_from_text("170141183460469231731687303715884105728",
                                                0),
        arrowlane::Code::BadHeader);
    AL_CHECK_ERR(arrowlane::detail::decimal128_from_text("1", -1),
                 arrowlane::Code::BadHeader);
    AL_CHECK_ERR(arrowlane::detail::decimal128_from_text("1", 39),
                 arrowlane::Code::BadHeader);
}

// The chunked digit writer must handle a value that fills all 38 digits, which
// is the only case that reaches the fifth base 10^9 chunk.
AL_TEST(decimal_text_covers_the_widest_value) {
    const std::string widest = std::string(38, '9');
    auto parsed = arrowlane::detail::decimal128_from_text(widest, 0);
    AL_REQUIRE(parsed.has_value());
    AL_CHECK_EQ(arrowlane::detail::decimal128_to_text(*parsed, 0), widest);

    // With scale 38 the text is the number itself, so the point has to be in it:
    // "999...9" would be 10^38-1 scaled up by 10^38 and overflow.
    const std::string widest_scaled = "0." + widest;
    auto parsed_scaled = arrowlane::detail::decimal128_from_text(widest_scaled, 38);
    AL_REQUIRE(parsed_scaled.has_value());
    AL_CHECK_EQ(arrowlane::detail::decimal128_to_text(*parsed_scaled, 38), widest_scaled);
}
