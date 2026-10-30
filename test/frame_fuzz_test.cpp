#include "harness.hpp"

#include "arrowlane/frame.hpp"

#include <cstddef>
#include <random>
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

AL_TEST(mutated_frames_never_crash) {
    auto good = arrowlane::encode_frame(make_test_frame());
    AL_REQUIRE(good.has_value());
    std::mt19937 rng(20261030);
    int decoded_count = 0;
    for (int iteration = 0; iteration < 2000; ++iteration) {
        auto mutated = *good;
        const std::size_t flips = 1 + (rng() % 4);
        for (std::size_t flip = 0; flip < flips; ++flip) {
            const std::size_t index = rng() % mutated.size();
            mutated[index] ^= std::byte(1U << (rng() % 8));
        }
        const auto decoded = arrowlane::decode_frame(mutated);
        if (decoded.has_value()) {
            ++decoded_count;
        }
    }
    AL_CHECK(decoded_count > 0);
}
