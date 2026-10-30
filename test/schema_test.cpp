#include "harness.hpp"

#include "arrowlane/schema.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "detail/bytes.hpp"

AL_TEST(schema_type_names) {
    AL_CHECK_EQ(arrowlane::Type::null().name(), std::string("null"));
    AL_CHECK_EQ(arrowlane::Type::boolean().name(), std::string("bool"));
    AL_CHECK_EQ(arrowlane::Type::integer(64, true).name(), std::string("int64"));
    AL_CHECK_EQ(arrowlane::Type::integer(8, false).name(), std::string("uint8"));
    AL_CHECK_EQ(arrowlane::Type::floating(16).name(), std::string("float16"));
    AL_CHECK_EQ(arrowlane::Type::large_utf8().name(), std::string("large_utf8"));
    AL_CHECK_EQ(arrowlane::Type::fixed_size_binary(3).name(),
                std::string("fixed_size_binary(3)"));
    AL_CHECK_EQ(arrowlane::Type::decimal(9, 2, 128).name(), std::string("decimal128(9,2)"));
    AL_CHECK_EQ(arrowlane::Type::timestamp(2, "UTC").name(), std::string("timestamp[us,UTC]"));
    AL_CHECK_EQ(arrowlane::Type::timestamp(0).name(), std::string("timestamp[s]"));
    AL_CHECK_EQ(arrowlane::Type::duration(1).name(), std::string("duration[ms]"));
}

AL_TEST(schema_widths) {
    AL_CHECK_EQ(arrowlane::Type::boolean().width(), std::size_t{1});
    AL_CHECK_EQ(arrowlane::Type::integer(64, true).width(), std::size_t{8});
    AL_CHECK_EQ(arrowlane::Type::utf8().width(), std::size_t{0});
    AL_CHECK(arrowlane::Type::utf8().is_variable_length());
    AL_CHECK(!arrowlane::Type::utf8().has_offsets_64());
    AL_CHECK_EQ(arrowlane::Type::large_binary().offset_width(), std::uint8_t{8});
    AL_CHECK_EQ(arrowlane::Type::decimal(9, 2, 128).width(), std::size_t{16});
    AL_CHECK_EQ(arrowlane::Type::date(0).width(), std::size_t{4});
    AL_CHECK_EQ(arrowlane::Type::date(1).width(), std::size_t{8});
    AL_CHECK_EQ(arrowlane::Type::fixed_size_binary(3).width(), std::size_t{3});
}

AL_TEST(schema_validation) {
    AL_CHECK(arrowlane::validate_type(arrowlane::Type::integer(64, true)).has_value());
    AL_CHECK_ERR(arrowlane::validate_type(arrowlane::Type::integer(24, true)),
                 arrowlane::Code::Invalid);
    AL_CHECK_ERR(arrowlane::validate_type(arrowlane::Type::floating(8)),
                 arrowlane::Code::Invalid);
    AL_CHECK_ERR(arrowlane::validate_type(arrowlane::Type::decimal(0, 0, 128)),
                 arrowlane::Code::Invalid);
    AL_CHECK_ERR(arrowlane::validate_type(arrowlane::Type::decimal(9, 10, 128)),
                 arrowlane::Code::Invalid);
    AL_CHECK_ERR(arrowlane::validate_type(arrowlane::Type::fixed_size_binary(0)),
                 arrowlane::Code::Invalid);
    AL_CHECK(arrowlane::validate_type(arrowlane::Type::decimal(38, 0, 128)).has_value());
    AL_CHECK(arrowlane::validate_type(arrowlane::Type::decimal(76, 6, 256)).has_value());
}

AL_TEST(schema_equality) {
    arrowlane::Schema left;
    left.fields.push_back(arrowlane::Field{"id", arrowlane::Type::integer(64, true), true});
    left.fields.push_back(arrowlane::Field{"name", arrowlane::Type::utf8(), true});
    arrowlane::Schema right = left;
    AL_CHECK(left == right);
    right.fields[1].nullable = false;
    AL_CHECK(!(left == right));
    right.fields[1].nullable = true;
    right.fields[1].type = arrowlane::Type::large_utf8();
    AL_CHECK(!(left == right));
}

AL_TEST(schema_message_frames_the_bytes) {
    arrowlane::Schema schema;
    schema.fields.push_back(arrowlane::Field{"id", arrowlane::Type::integer(64, true), true});
    auto message = arrowlane::encode_schema_message(schema);
    AL_REQUIRE(message.has_value());
    AL_CHECK_EQ(static_cast<std::uint8_t>((*message)[0]), std::uint8_t{0xFF});
    AL_CHECK_EQ(static_cast<std::uint8_t>((*message)[3]), std::uint8_t{0xFF});
    const std::uint32_t meta =
        arrowlane::detail::load_le<std::uint32_t>(message->data() + 4);
    AL_CHECK_EQ(meta % 8, std::uint32_t{0});
    AL_CHECK_EQ(message->size(), std::size_t{8} + meta);
}

AL_TEST(schema_message_type_tables) {
    arrowlane::Schema schema;
    schema.fields.push_back(arrowlane::Field{"d32", arrowlane::Type::date(0), true});
    schema.fields.push_back(arrowlane::Field{"d64", arrowlane::Type::date(1), true});
    schema.fields.push_back(arrowlane::Field{"t64", arrowlane::Type::time(3, 64), true});
    schema.fields.push_back(
        arrowlane::Field{"ts", arrowlane::Type::timestamp(2, "UTC"), true});
    schema.fields.push_back(
        arrowlane::Field{"dec", arrowlane::Type::decimal(9, 2, 128), true});
    schema.fields.push_back(
        arrowlane::Field{"fsb", arrowlane::Type::fixed_size_binary(3), true});
    schema.fields.push_back(arrowlane::Field{"u8", arrowlane::Type::integer(8, false), true});
    auto message = arrowlane::encode_schema_message(schema);
    AL_REQUIRE(message.has_value());
    auto decoded = arrowlane::decode_schema_message(*message);
    AL_REQUIRE(decoded.has_value());
    AL_CHECK(*decoded == schema);
}

AL_TEST(schema_base64_roundtrip) {
    arrowlane::Schema schema;
    schema.fields.push_back(arrowlane::Field{"ts", arrowlane::Type::timestamp(0), true});
    auto text = arrowlane::schema_base64(schema);
    AL_REQUIRE(text.has_value());
    auto decoded = arrowlane::schema_from_base64(*text);
    AL_REQUIRE(decoded.has_value());
    AL_CHECK(*decoded == schema);
}

AL_TEST(schema_message_rejects_bad_input) {
    const std::array<std::byte, 8> short_bytes{
        std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
        std::byte{8},    std::byte{0},    std::byte{0},    std::byte{0}};
    AL_CHECK_ERR(arrowlane::decode_schema_message(short_bytes),
                 arrowlane::Code::TruncatedFrame);
    const std::vector<std::byte> empty(8);
    AL_CHECK_ERR(arrowlane::decode_schema_message(empty), arrowlane::Code::BadHeader);
}
