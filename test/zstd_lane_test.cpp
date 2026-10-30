#include "harness.hpp"

#include "arrowlane/digest.hpp"
#include "arrowlane/fixture.hpp"
#include "arrowlane/lanes.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace {

void check_matches(const arrowlane::RecordBatch& decoded,
                   const arrowlane::RecordBatch& expected) {
    AL_CHECK(decoded.schema == expected.schema);
    AL_CHECK_EQ(decoded.rows(), expected.rows());
    for (std::size_t index = 0; index < expected.columns.size(); ++index) {
        AL_CHECK_EQ(arrowlane::column_digest(decoded.columns[index],
                                             decoded.schema.fields[index].type),
                    arrowlane::column_digest(expected.columns[index],
                                             expected.schema.fields[index].type));
    }
}

}  // namespace

AL_TEST(zstd_lane_roundtrips_three_rows) {
    using namespace arrowlane;
    const auto batch = make_fixture(3);
    auto frame = encode_lane(LaneId::IpcZstd, batch);
    AL_REQUIRE(frame.has_value());
    AL_CHECK(frame->lane == LaneId::IpcZstd);
    AL_CHECK(frame->payload.size() < frame->uncompressed_len);
    auto decoded = decode_lane(*frame);
    AL_REQUIRE(decoded.has_value());
    check_matches(*decoded, batch);
}

AL_TEST(zstd_lane_roundtrips_five_thousand_rows) {
    using namespace arrowlane;
    const auto batch = make_fixture(5000);
    LaneOptions options;
    options.level = 9;
    auto frame = encode_lane(LaneId::IpcZstd, batch, options);
    AL_REQUIRE(frame.has_value());
    auto decoded = decode_lane(*frame);
    AL_REQUIRE(decoded.has_value());
    check_matches(*decoded, batch);
}

AL_TEST(zstd_lane_rejects_a_corrupt_payload) {
    using namespace arrowlane;
    const auto batch = make_fixture(3);
    auto frame = encode_lane(LaneId::IpcZstd, batch);
    AL_REQUIRE(frame.has_value());
    frame->payload[0] = static_cast<std::byte>(
        static_cast<unsigned>(frame->payload[0]) ^ 0xFFu);
    AL_CHECK_ERR(decode_lane(*frame), Code::Frame);
}

AL_TEST(zstd_lane_rejects_bad_levels) {
    using namespace arrowlane;
    const auto batch = make_fixture(3);
    LaneOptions low;
    low.level = 0;
    AL_CHECK_ERR(encode_lane(LaneId::IpcZstd, batch, low), Code::Invalid);
    LaneOptions high;
    high.level = 20;
    AL_CHECK_ERR(encode_lane(LaneId::IpcZstd, batch, high), Code::Invalid);
}
