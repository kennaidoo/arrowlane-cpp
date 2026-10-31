#include "harness.hpp"

#include "arrowlane/crc32.hpp"
#include "arrowlane/fixture.hpp"
#include "arrowlane/frame.hpp"
#include "arrowlane/lanes.hpp"
#include "arrowlane/json.hpp"
#include "detail/bytes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <sstream>
#include <string>

namespace {
arrowlane::Frame make_test_frame() {
    arrowlane::Frame frame;
    frame.lane = arrowlane::LaneId::Json;
    frame.rows = 3;
    frame.uncompressed_len = 2;
    frame.payload = {std::byte{'h'}, std::byte{'i'}};
    frame.header = arrowlane::JsonValue::make_object();
    frame.header.set("schema", arrowlane::JsonValue(std::string("abc")));
    return frame;
}
}  // namespace

AL_TEST(load_store_roundtrip) {
    std::array<std::byte, 8> buffer{};
    arrowlane::detail::store_le<std::uint64_t>(buffer.data(), 0x0102030405060708ULL);
    AL_CHECK_EQ(buffer[0], std::byte{0x08});
    AL_CHECK_EQ(buffer[7], std::byte{0x01});
    AL_CHECK_EQ(arrowlane::detail::load_le<std::uint64_t>(buffer.data()),
                0x0102030405060708ULL);
}

AL_TEST(frame_prefix_is_exact) {
    arrowlane::Frame frame;
    frame.lane = arrowlane::LaneId::Json;
    frame.rows = 3;
    frame.uncompressed_len = 2;
    frame.payload = {std::byte{'h'}, std::byte{'i'}};
    frame.header = arrowlane::JsonValue::make_object();
    frame.header.set("schema", arrowlane::JsonValue(std::string("abc")));

    auto encoded = arrowlane::encode_frame(frame);
    AL_REQUIRE(encoded.has_value());
    const auto& bytes = *encoded;
    const std::uint16_t header_len =
        arrowlane::detail::load_le<std::uint16_t>(bytes.data() + 36);
    AL_REQUIRE(bytes.size() == arrowlane::kPrefixSize + header_len + 2);
    AL_CHECK_EQ(bytes[0], std::byte{'A'});
    AL_CHECK_EQ(bytes[3], std::byte{'1'});
    AL_CHECK_EQ(bytes[4], std::byte{1});
    AL_CHECK_EQ(bytes[5], std::byte{1});
    AL_CHECK_EQ(arrowlane::crc32(std::span<const std::byte>(frame.payload)),
                0xD8932AACU);
}

AL_TEST(frame_rejects_each_fault) {
    auto good = arrowlane::encode_frame(make_test_frame());
    AL_REQUIRE(good.has_value());

    const std::span<const std::byte> short_input(good->data(), 10);
    AL_CHECK_ERR(arrowlane::decode_frame(short_input), arrowlane::Code::TruncatedFrame);

    auto bad_magic = *good;
    bad_magic[0] = std::byte{'X'};
    AL_CHECK_ERR(arrowlane::decode_frame(bad_magic), arrowlane::Code::BadMagic);

    auto bad_version = *good;
    bad_version[4] = std::byte{9};
    AL_CHECK_ERR(arrowlane::decode_frame(bad_version), arrowlane::Code::UnsupportedVersion);

    auto bad_lane = *good;
    bad_lane[5] = std::byte{9};
    AL_CHECK_ERR(arrowlane::decode_frame(bad_lane), arrowlane::Code::UnknownLane);

    auto bad_crc = *good;
    bad_crc.back() ^= std::byte{0xFF};
    AL_CHECK_ERR(arrowlane::decode_frame(bad_crc), arrowlane::Code::ChecksumMismatch);

    auto trailing = *good;
    trailing.push_back(std::byte{0});
    AL_CHECK_ERR(arrowlane::decode_frame(trailing), arrowlane::Code::TruncatedFrame);

    auto decoded = arrowlane::decode_frame(*good);
    AL_REQUIRE(decoded.has_value());
    AL_CHECK_EQ(decoded->rows, std::uint64_t{3});
    AL_CHECK_EQ(decoded->payload.size(), std::size_t{2});
}

AL_TEST(frame_stream_roundtrip) {
    const auto frame = make_test_frame();
    std::ostringstream out;
    auto written = arrowlane::write_frame(out, frame);
    AL_REQUIRE(written.has_value());
    AL_CHECK_EQ(*written, static_cast<std::uint64_t>(out.str().size()));

    std::istringstream in(out.str());
    auto read = arrowlane::read_frame(in);
    AL_REQUIRE(read.has_value());
    AL_REQUIRE(read->has_value());
    AL_CHECK_EQ((*read)->rows, std::uint64_t{3});

    auto end = arrowlane::read_frame(in);
    AL_REQUIRE(end.has_value());
    AL_CHECK(!end->has_value());
}

AL_TEST(frame_stream_truncated_payload) {
    std::ostringstream out;
    AL_REQUIRE(arrowlane::write_frame(out, make_test_frame()).has_value());
    const std::string bytes = out.str();
    std::istringstream in(bytes.substr(0, bytes.size() - 1));
    AL_CHECK_ERR(arrowlane::read_frame(in), arrowlane::Code::TruncatedFrame);
}

namespace {

arrowlane::JsonValue chunk_header(const char* id, std::int64_t index, bool last) {
    arrowlane::JsonValue value = arrowlane::JsonValue::make_object();
    value.set("id", arrowlane::JsonValue(std::string(id)));
    value.set("index", arrowlane::JsonValue(index));
    value.set("last", arrowlane::JsonValue(last));
    return value;
}

}  // namespace

AL_TEST(chunk_metadata_accepts_a_valid_triple) {
    const auto value = chunk_header("0123456789abcdef0123456789abcdef", 0, false);
    auto parsed = arrowlane::parse_chunk(value);
    AL_REQUIRE(parsed.has_value());
    AL_CHECK_EQ(parsed->id.size(), std::size_t{32});
    AL_CHECK_EQ(parsed->index, std::uint64_t{0});
    AL_CHECK(!parsed->last);
}

AL_TEST(chunk_metadata_rejects_uppercase) {
    const auto value = chunk_header("0123456789ABCDEF0123456789abcdef", 0, false);
    AL_CHECK_ERR(arrowlane::parse_chunk(value), arrowlane::Code::Chunk);
}

AL_TEST(chunk_metadata_rejects_a_short_id) {
    const auto value = chunk_header("0123456789abcdef0123456789abcde", 0, false);
    AL_CHECK_ERR(arrowlane::parse_chunk(value), arrowlane::Code::Chunk);
}

AL_TEST(chunk_metadata_rejects_the_index_cap) {
    const auto value = chunk_header("0123456789abcdef0123456789abcdef", 1'000'000, false);
    AL_CHECK_ERR(arrowlane::parse_chunk(value), arrowlane::Code::Chunk);
}

AL_TEST(chunk_metadata_rejects_a_missing_key) {
    arrowlane::JsonValue value = arrowlane::JsonValue::make_object();
    value.set("id", arrowlane::JsonValue(std::string("0123456789abcdef0123456789abcdef")));
    value.set("index", arrowlane::JsonValue(std::int64_t{0}));
    AL_CHECK_ERR(arrowlane::parse_chunk(value), arrowlane::Code::Chunk);
}

// The lane reserves the prefix and header space, so encode_frame must fill that
// space and hand back the same buffer. A copy would show up as a different
// address.
AL_TEST(encode_frame_writes_in_place_when_headroom_matches) {
    const auto batch = arrowlane::make_fixture(64);
    auto frame = arrowlane::encode_lane(arrowlane::LaneId::Ipc, batch);
    AL_REQUIRE(frame.has_value());
    AL_CHECK(frame->headroom > 0);
    const std::byte* address = frame->payload.data();
    const std::size_t total = frame->payload.size();
    auto wire = arrowlane::encode_frame(std::move(*frame));
    AL_REQUIRE(wire.has_value());
    AL_CHECK(wire->data() == address);
    AL_CHECK_EQ(wire->size(), total);
    auto decoded =
        arrowlane::decode_frame(std::span<const std::byte>(wire->data(), wire->size()));
    AL_REQUIRE(decoded.has_value());
    AL_CHECK_EQ(decoded->rows, std::uint64_t{64});
    AL_CHECK(decoded->uncompressed_len > 0);
}
