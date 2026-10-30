#include "harness.hpp"

#include "detail/flatbuf.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

AL_TEST(flatbuffer_table_roundtrip) {
    arrowlane::detail::FlatWriter writer;
    auto root = writer.begin_table(3);
    writer.add_i16(root, 0, 4);
    writer.add_u8(root, 1, 1);
    const std::size_t text_field = writer.add_offset(root, 2);
    writer.end_table(root);
    const std::size_t text = writer.add_string("hello");
    writer.patch_offset(text_field, text);
    const auto bytes = writer.finish(root.pos);

    arrowlane::detail::FlatReader reader(bytes);
    auto table = reader.root();
    AL_REQUIRE(table.has_value());
    const auto value = reader.i16(*table, 0);
    AL_REQUIRE(value.has_value());
    AL_CHECK_EQ(*value, std::int16_t{4});
    const auto tag = reader.u8(*table, 1);
    AL_REQUIRE(tag.has_value());
    AL_CHECK_EQ(*tag, std::uint8_t{1});
    const auto string = reader.string(*table, 2);
    AL_REQUIRE(string.has_value());
    AL_CHECK_EQ(*string, std::string_view("hello"));
}

AL_TEST(flatbuffer_reader_rejects_bad_offsets) {
    const std::array<std::byte, 8> data{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                                        std::byte{0x7F}, std::byte{0}, std::byte{0},
                                        std::byte{0}, std::byte{0}};
    arrowlane::detail::FlatReader reader(data);
    AL_CHECK(!reader.root().has_value());
}

AL_TEST(flatbuffer_shares_identical_vtables) {
    arrowlane::detail::FlatWriter writer;
    auto first = writer.begin_table(1);
    writer.add_i32(first, 0, 7);
    writer.end_table(first);
    auto second = writer.begin_table(1);
    writer.add_i32(second, 0, 9);
    writer.end_table(second);
    const auto bytes = writer.finish(first.pos);

    arrowlane::detail::FlatReader reader(bytes);
    const auto left = reader.table_at(first.pos);
    const auto right = reader.table_at(second.pos);
    AL_REQUIRE(left.has_value());
    AL_REQUIRE(right.has_value());
    AL_CHECK_EQ(left->vtable, right->vtable);
    AL_CHECK_EQ(reader.i32(*left, 0).value_or(0), 7);
    AL_CHECK_EQ(reader.i32(*right, 0).value_or(0), 9);
}
