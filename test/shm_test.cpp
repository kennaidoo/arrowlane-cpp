#include "harness.hpp"

#include "arrowlane/digest.hpp"
#include "arrowlane/fixture.hpp"
#include "arrowlane/lanes.hpp"
#include "arrowlane/shm.hpp"

#include <cstddef>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>

#include <sys/stat.h>
#endif

namespace {

const char* kToken = "0123456789abcdef-token";

arrowlane::ShmDescriptor sample_descriptor() {
    arrowlane::ShmDescriptor descriptor;
    descriptor.name = "al-0123456789abcdef0123456789abcdef";
    descriptor.length = 128;
    descriptor.crc32 = 0xDEADBEEFu;
    descriptor.token = kToken;
    return descriptor;
}

}  // namespace

AL_TEST(descriptor_round_trips) {
    const auto descriptor = sample_descriptor();
    const std::string text = descriptor.to_json();
    AL_CHECK_EQ(text.rfind("{\"crc32\":", 0), std::size_t{0});
    auto parsed = arrowlane::ShmDescriptor::from_json(text);
    AL_REQUIRE(parsed.has_value());
    AL_CHECK_EQ(parsed->name, descriptor.name);
    AL_CHECK_EQ(parsed->length, descriptor.length);
    AL_CHECK_EQ(parsed->crc32, descriptor.crc32);
    AL_CHECK_EQ(parsed->token, descriptor.token);
    AL_CHECK_EQ(parsed->offset, std::uint64_t{0});
}

AL_TEST(descriptor_rejects_unknown_keys) {
    const std::string text =
        R"({"crc32":1,"length":8,"name":"al-0123456789abcdef0123456789abcdef",)"
        R"("token":"0123456789abcdef-token","extra":1})";
    AL_CHECK_ERR(arrowlane::ShmDescriptor::from_json(text), arrowlane::Code::Segment);
}

AL_TEST(descriptor_rejects_a_bad_name) {
    const std::string text =
        R"({"crc32":1,"length":8,"name":"other-0123456789abcdef0123456789abcdef",)"
        R"("token":"0123456789abcdef-token"})";
    AL_CHECK_ERR(arrowlane::ShmDescriptor::from_json(text), arrowlane::Code::Segment);
    const std::string slash =
        R"({"crc32":1,"length":8,"name":"al-0123/456789abcdef0123456789abcdef",)"
        R"("token":"0123456789abcdef-token"})";
    AL_CHECK_ERR(arrowlane::ShmDescriptor::from_json(slash), arrowlane::Code::Segment);
}

AL_TEST(descriptor_rejects_a_bad_length) {
    const std::string text =
        R"({"crc32":1,"length":0,"name":"al-0123456789abcdef0123456789abcdef",)"
        R"("token":"0123456789abcdef-token"})";
    AL_CHECK_ERR(arrowlane::ShmDescriptor::from_json(text), arrowlane::Code::Segment);
}

AL_TEST(descriptor_rejects_a_short_token) {
    const std::string text =
        R"({"crc32":1,"length":8,"name":"al-0123456789abcdef0123456789abcdef",)"
        R"("token":"short"})";
    AL_CHECK_ERR(arrowlane::ShmDescriptor::from_json(text), arrowlane::Code::Segment);
}

AL_TEST(descriptor_rejects_a_bad_crc) {
    const std::string text =
        R"({"crc32":-1,"length":8,"name":"al-0123456789abcdef0123456789abcdef",)"
        R"("token":"0123456789abcdef-token"})";
    AL_CHECK_ERR(arrowlane::ShmDescriptor::from_json(text), arrowlane::Code::Segment);
}

AL_TEST(descriptor_rejects_a_negative_offset) {
    const std::string text =
        R"({"crc32":1,"length":8,"name":"al-0123456789abcdef0123456789abcdef",)"
        R"("offset":-4,"token":"0123456789abcdef-token"})";
    AL_CHECK_ERR(arrowlane::ShmDescriptor::from_json(text), arrowlane::Code::Segment);
}

#if !defined(_WIN32)

namespace {

std::string test_directory() {
    const std::string path = "/tmp/arrowlane-shm-test-" +
                             std::to_string(static_cast<long long>(::getpid()));
    ::mkdir(path.c_str(), 0700);
    return path;
}

std::vector<std::byte> sample_bytes(std::size_t count) {
    std::vector<std::byte> bytes(count);
    for (std::size_t index = 0; index < count; ++index) {
        bytes[index] = static_cast<std::byte>(index % 251);
    }
    return bytes;
}

}  // namespace

AL_TEST(segment_round_trips_and_is_removed) {
    const std::string directory = test_directory();
    AL_CHECK(arrowlane::is_ready(directory));
    const auto data = sample_bytes(4096);
    auto descriptor = arrowlane::write_segment(data, directory, kToken);
    AL_REQUIRE(descriptor.has_value());
    AL_CHECK_EQ(descriptor->name.rfind("al-", 0), std::size_t{0});
    AL_CHECK_EQ(descriptor->length, std::uint64_t{4096});

    auto read_back = arrowlane::read_segment(*descriptor, directory, kToken);
    AL_REQUIRE(read_back.has_value());
    AL_CHECK_EQ(read_back->size(), data.size());
    AL_CHECK(*read_back == data);

    // read_segment unlinks the name, so a second read must fail.
    auto second = arrowlane::read_segment(*descriptor, directory, kToken);
    AL_CHECK(!second.has_value());
}

AL_TEST(segment_rejects_a_wrong_token) {
    const std::string directory = test_directory();
    auto descriptor =
        arrowlane::write_segment(sample_bytes(256), directory, kToken);
    AL_REQUIRE(descriptor.has_value());
    auto wrong = arrowlane::read_segment(*descriptor, directory,
                                         "ffffffffffffffff-token");
    AL_CHECK_ERR(wrong, arrowlane::Code::Auth);
    AL_CHECK(arrowlane::discard(*descriptor, directory).has_value());
}

AL_TEST(segment_detects_a_crc_mismatch) {
    const std::string directory = test_directory();
    auto descriptor = arrowlane::write_segment(sample_bytes(512), directory, kToken);
    AL_REQUIRE(descriptor.has_value());
    auto tampered = *descriptor;
    tampered.crc32 ^= 0xFFu;
    auto read_back = arrowlane::read_segment(tampered, directory, kToken);
    AL_CHECK_ERR(read_back, arrowlane::Code::ChecksumMismatch);
}

AL_TEST(sweep_removes_stale_segments) {
    const std::string directory = test_directory();
    auto descriptor = arrowlane::write_segment(sample_bytes(64), directory, kToken);
    AL_REQUIRE(descriptor.has_value());
    auto removed = arrowlane::sweep(directory, -1.0);
    AL_REQUIRE(removed.has_value());
    AL_CHECK(*removed >= std::size_t{1});
    AL_CHECK(!arrowlane::read_segment(*descriptor, directory, kToken).has_value());
}

AL_TEST(shm_lane_round_trips_a_batch) {
    const std::string directory = test_directory();
    const auto batch = arrowlane::make_fixture(64);
    arrowlane::LaneOptions options;
    options.token = kToken;
    options.directory = directory;
    auto frame = arrowlane::encode_lane(arrowlane::LaneId::Shm, batch, options);
    AL_REQUIRE(frame.has_value());
    AL_CHECK_EQ(frame->rows, std::uint64_t{64});
    AL_CHECK(frame->uncompressed_len > 0);
    auto decoded = arrowlane::decode_lane(*frame, options);
    AL_REQUIRE(decoded.has_value());
    AL_CHECK_EQ(decoded->rows(), batch.rows());
    AL_CHECK(arrowlane::validate_batch(*decoded).has_value());
    for (std::size_t index = 0; index < batch.columns.size(); ++index) {
        AL_CHECK_EQ(arrowlane::column_digest(decoded->columns[index],
                                             decoded->schema.fields[index].type),
                    arrowlane::column_digest(batch.columns[index],
                                             batch.schema.fields[index].type));
    }
}

AL_TEST(shm_lane_needs_a_token) {
    const std::string directory = test_directory();
    arrowlane::LaneOptions options;
    options.directory = directory;
    auto frame = arrowlane::encode_lane(arrowlane::LaneId::Shm,
                                        arrowlane::make_fixture(4), options);
    AL_CHECK_ERR(frame, arrowlane::Code::LaneUnavailable);
}

#else

AL_TEST(shm_lane_is_unavailable_on_windows) {
    AL_CHECK(!arrowlane::is_ready());
    arrowlane::LaneOptions options;
    options.token = kToken;
    auto frame = arrowlane::encode_lane(arrowlane::LaneId::Shm,
                                        arrowlane::make_fixture(4), options);
    AL_CHECK_ERR(frame, arrowlane::Code::LaneUnavailable);
}

#endif
