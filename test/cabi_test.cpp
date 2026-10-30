#include "harness.hpp"

#include "arrowlane/cabi.h"

#include "arrowlane/fixture.hpp"
#include "arrowlane/frame.hpp"
#include "arrowlane/lanes.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

AL_TEST(version_is_not_empty) {
    const char* version = al_version();
    AL_REQUIRE(version != nullptr);
    AL_CHECK(std::strlen(version) > 0);
}

AL_TEST(select_matches_the_oracle) {
    AlDecision decision{};
    AL_CHECK_EQ(al_select(200000, 13380000, 1, 0, false, &decision), 0);
    AL_CHECK_EQ(decision.lane_id, std::uint8_t{2});
    AL_CHECK(std::fabs(decision.predicted_ms - 14.8256) < 1e-4);
    AL_CHECK_EQ(decision.predicted_bytes, std::uint64_t{13380454});
    AL_REQUIRE(decision.reason != nullptr);
    AL_CHECK_EQ(std::string(decision.reason), std::string("BEST"));
    al_free_reason(decision.reason);
}

AL_TEST(select_rejects_bad_arguments) {
    AlDecision decision{};
    AL_CHECK_EQ(al_select(1, 1, 9, 0, false, &decision), AL_ERR_INVALID);
    AL_CHECK_EQ(al_select(1, 1, 0, 0, false, nullptr), AL_ERR_INVALID);
}

AL_TEST(inspect_reads_a_frame) {
    auto frame = arrowlane::encode_lane(arrowlane::LaneId::Json,
                                        arrowlane::make_fixture(3));
    AL_REQUIRE(frame.has_value());
    auto wire = arrowlane::encode_frame(*frame);
    AL_REQUIRE(wire.has_value());
    AlBuffer out = al_inspect(reinterpret_cast<const std::uint8_t*>(wire->data()),
                              wire->size());
    AL_REQUIRE(out.data != nullptr);
    const std::string text(reinterpret_cast<const char*>(out.data), out.len);
    AL_CHECK(text.find("\"rows\":3") != std::string::npos);
    AL_CHECK(text.find("\"columns\":8") != std::string::npos);
    AL_CHECK(text.find("\"digests\"") != std::string::npos);
    al_free(out);
}

AL_TEST(inspect_rejects_garbage) {
    const std::uint8_t junk[4] = {1, 2, 3, 4};
    const AlBuffer out = al_inspect(junk, 4);
    AL_CHECK(out.data == nullptr);
    AL_CHECK_EQ(out.len, std::size_t{0});
}

AL_TEST(repack_moves_a_frame_to_another_lane) {
    auto frame = arrowlane::encode_lane(arrowlane::LaneId::Json,
                                        arrowlane::make_fixture(3));
    AL_REQUIRE(frame.has_value());
    auto wire = arrowlane::encode_frame(*frame);
    AL_REQUIRE(wire.has_value());
    AlBuffer out = al_repack(reinterpret_cast<const std::uint8_t*>(wire->data()),
                             wire->size(), 2);
    AL_REQUIRE(out.data != nullptr);
    const auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(out.data), out.len);
    auto decoded = arrowlane::decode_frame(bytes);
    AL_REQUIRE(decoded.has_value());
    AL_CHECK(decoded->lane == arrowlane::LaneId::Ipc);
    AL_CHECK_EQ(decoded->rows, std::uint64_t{3});
    al_free(out);
}

AL_TEST(repack_rejects_an_unknown_lane) {
    auto frame = arrowlane::encode_lane(arrowlane::LaneId::Json,
                                        arrowlane::make_fixture(3));
    AL_REQUIRE(frame.has_value());
    auto wire = arrowlane::encode_frame(*frame);
    AL_REQUIRE(wire.has_value());
    const AlBuffer out = al_repack(
        reinterpret_cast<const std::uint8_t*>(wire->data()), wire->size(), 9);
    AL_CHECK(out.data == nullptr);
}
