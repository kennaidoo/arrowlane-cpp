#include "harness.hpp"

#include "arrowlane/fixture.hpp"
#include "arrowlane/ipc.hpp"
#include "arrowlane/lanes.hpp"

#include <cstddef>
#include <cstdint>

namespace {

// xorshift32: deterministic and dependency free.
struct Rng {
    std::uint32_t state = 0x2545F491u;
    std::uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
};

void mutate(std::vector<std::byte>& bytes, Rng& rng) {
    const std::size_t flips = 1 + rng.next() % 4;
    for (std::size_t flip = 0; flip < flips; ++flip) {
        const std::size_t at = rng.next() % bytes.size();
        bytes[at] = static_cast<std::byte>(static_cast<unsigned>(bytes[at]) ^
                                           (1u << (rng.next() % 8)));
    }
}

}  // namespace

AL_TEST(frames_survive_mutations) {
    using namespace arrowlane;
    const auto batch = make_fixture(8);
    auto frame = encode_lane(LaneId::Ipc, batch);
    AL_REQUIRE(frame.has_value());
    auto wire = encode_frame(*frame);
    AL_REQUIRE(wire.has_value());
    Rng rng;
    std::size_t rejected = 0;
    for (int iteration = 0; iteration < 5000; ++iteration) {
        auto mutated = *wire;
        mutate(mutated, rng);
        auto decoded = decode_frame(mutated);
        if (!decoded) {
            ++rejected;
            continue;
        }
        auto rows = decode_lane(*decoded);
        if (!rows) {
            ++rejected;
            continue;
        }
        // The header is not covered by the CRC, so a mutated header can still
        // decode. What must never happen is an inconsistent batch.
        AL_CHECK(validate_batch(*rows).has_value());
    }
    AL_CHECK(rejected > 0);
}

AL_TEST(ipc_streams_survive_mutations) {
    using namespace arrowlane;
    auto stream = encode_ipc(make_fixture(8));
    AL_REQUIRE(stream.has_value());
    Rng rng;
    rng.state = 0x9E3779B9u;
    std::size_t rejected = 0;
    for (int iteration = 0; iteration < 5000; ++iteration) {
        auto mutated = *stream;
        mutate(mutated, rng);
        auto decoded = decode_ipc(mutated);
        if (!decoded) {
            ++rejected;
            continue;
        }
        auto valid = validate_batch(*decoded);
        AL_CHECK(valid.has_value());
    }
    AL_CHECK(rejected > 0);
}

AL_TEST(schema_messages_survive_mutations) {
    using namespace arrowlane;
    auto message = encode_schema_message(make_fixture(8).schema);
    AL_REQUIRE(message.has_value());
    Rng rng;
    rng.state = 0x1234567u;
    std::size_t rejected = 0;
    for (int iteration = 0; iteration < 5000; ++iteration) {
        auto mutated = *message;
        mutate(mutated, rng);
        auto decoded = decode_schema_message(mutated);
        if (!decoded) {
            ++rejected;
            continue;
        }
        AL_CHECK(validate_schema(*decoded).has_value());
    }
    AL_CHECK(rejected > 0);
}
