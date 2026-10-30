#include "harness.hpp"

#include "arrowlane/chunks.hpp"
#include "arrowlane/fixture.hpp"
#include "arrowlane/frame.hpp"
#include "arrowlane/ipc.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

std::vector<std::vector<std::byte>> take_frames(arrowlane::ChunkProducer& producer,
                                                std::size_t limit = 1000) {
    std::vector<std::vector<std::byte>> frames;
    std::vector<std::byte> wire;
    while (frames.size() < limit && producer.next(wire)) {
        frames.push_back(wire);
    }
    return frames;
}

std::span<const std::byte> span_of(const std::vector<std::byte>& bytes) {
    return std::span<const std::byte>(bytes.data(), bytes.size());
}

}  // namespace

AL_TEST(producer_splits_three_thousand_rows) {
    arrowlane::ChunkProducer producer(arrowlane::make_fixture(3000),
                                      arrowlane::LaneId::Ipc, 1000,
                                      arrowlane::kMaxPayloadBytes);
    AL_CHECK_EQ(producer.encoded_chunks(), std::uint64_t{0});
    const auto frames = take_frames(producer);
    AL_CHECK(!producer.failed());
    AL_REQUIRE(frames.size() == 3);
    AL_CHECK_EQ(producer.encoded_chunks(), std::uint64_t{3});
    std::int64_t rows = 0;
    for (std::size_t index = 0; index < frames.size(); ++index) {
        auto frame = arrowlane::decode_frame(span_of(frames[index]));
        AL_REQUIRE(frame.has_value());
        const auto info = frame->chunk();
        AL_REQUIRE(info.has_value());
        AL_CHECK_EQ(info->index, static_cast<std::uint64_t>(index));
        AL_CHECK(info->last == (index + 1 == frames.size()));
        AL_CHECK_EQ(info->id, producer.stream_id());
        rows += static_cast<std::int64_t>(frame->rows);
    }
    AL_CHECK_EQ(rows, std::int64_t{3000});
}

AL_TEST(producer_splits_an_oversize_chunk) {
    arrowlane::ChunkProducer producer(arrowlane::make_fixture(3000),
                                      arrowlane::LaneId::Ipc, 1000, 4000);
    const auto frames = take_frames(producer);
    AL_CHECK(!producer.failed());
    AL_CHECK(frames.size() > 3);
    std::int64_t rows = 0;
    for (std::size_t index = 0; index < frames.size(); ++index) {
        auto frame = arrowlane::decode_frame(span_of(frames[index]));
        AL_REQUIRE(frame.has_value());
        const auto info = frame->chunk();
        AL_REQUIRE(info.has_value());
        AL_CHECK_EQ(info->index, static_cast<std::uint64_t>(index));
        AL_CHECK(info->last == (index + 1 == frames.size()));
        AL_CHECK(frame->payload.size() <= 4000);
        rows += static_cast<std::int64_t>(frame->rows);
    }
    AL_CHECK_EQ(rows, std::int64_t{3000});
}

AL_TEST(producer_encodes_nothing_before_next) {
    arrowlane::ChunkProducer producer(arrowlane::make_fixture(10), arrowlane::LaneId::Json,
                                      5, arrowlane::kMaxPayloadBytes);
    AL_CHECK_EQ(producer.encoded_chunks(), std::uint64_t{0});
    std::vector<std::byte> wire;
    AL_CHECK(producer.next(wire));
    AL_CHECK_EQ(producer.encoded_chunks(), std::uint64_t{1});
    AL_CHECK(!wire.empty());
}

AL_TEST(producer_stream_ids_are_distinct_hex) {
    arrowlane::ChunkProducer first(arrowlane::make_fixture(4), arrowlane::LaneId::Json, 4,
                                   arrowlane::kMaxPayloadBytes);
    arrowlane::ChunkProducer second(arrowlane::make_fixture(4), arrowlane::LaneId::Json, 4,
                                    arrowlane::kMaxPayloadBytes);
    AL_CHECK_EQ(first.stream_id().size(), std::size_t{32});
    for (const char character : first.stream_id()) {
        const bool hex = (character >= '0' && character <= '9') ||
                         (character >= 'a' && character <= 'f');
        AL_CHECK(hex);
    }
    AL_CHECK(first.stream_id() != second.stream_id());
}

AL_TEST(assembler_round_trips_a_stream) {
    arrowlane::ChunkProducer producer(arrowlane::make_fixture(3000),
                                      arrowlane::LaneId::Ipc, 1000,
                                      arrowlane::kMaxPayloadBytes);
    const auto frames = take_frames(producer);
    AL_REQUIRE(frames.size() == 3);
    arrowlane::ChunkAssembler assembler;
    std::int64_t rows = 0;
    for (const auto& wire : frames) {
        auto batch = assembler.add(span_of(wire));
        AL_REQUIRE(batch.has_value());
        rows += batch->rows();
        AL_CHECK(arrowlane::validate_batch(*batch).has_value());
    }
    AL_CHECK_EQ(rows, std::int64_t{3000});
    AL_CHECK_EQ(assembler.count(), std::uint64_t{3});
    AL_CHECK(assembler.finished());
    AL_CHECK(assembler.check_complete().has_value());
    AL_CHECK_EQ(assembler.stream_id(), producer.stream_id());
}

AL_TEST(assembler_rejects_a_gap) {
    arrowlane::ChunkProducer producer(arrowlane::make_fixture(3000),
                                      arrowlane::LaneId::Ipc, 1000,
                                      arrowlane::kMaxPayloadBytes);
    const auto frames = take_frames(producer);
    AL_REQUIRE(frames.size() == 3);
    arrowlane::ChunkAssembler assembler;
    AL_CHECK(assembler.add(span_of(frames[0])).has_value());
    AL_CHECK_ERR(assembler.add(span_of(frames[2])), arrowlane::Code::Chunk);
}

AL_TEST(assembler_rejects_a_duplicate) {
    arrowlane::ChunkProducer producer(arrowlane::make_fixture(3000),
                                      arrowlane::LaneId::Ipc, 1000,
                                      arrowlane::kMaxPayloadBytes);
    const auto frames = take_frames(producer);
    AL_REQUIRE(frames.size() == 3);
    arrowlane::ChunkAssembler assembler;
    AL_CHECK(assembler.add(span_of(frames[0])).has_value());
    AL_CHECK_ERR(assembler.add(span_of(frames[0])), arrowlane::Code::Chunk);
}

AL_TEST(assembler_rejects_a_mixed_stream_id) {
    arrowlane::ChunkProducer first(arrowlane::make_fixture(3000), arrowlane::LaneId::Ipc,
                                   1000, arrowlane::kMaxPayloadBytes);
    arrowlane::ChunkProducer second(arrowlane::make_fixture(3000), arrowlane::LaneId::Ipc,
                                    1000, arrowlane::kMaxPayloadBytes);
    const auto first_frames = take_frames(first, 1);
    const auto second_frames = take_frames(second, 1);
    AL_REQUIRE(first_frames.size() == 1);
    AL_REQUIRE(second_frames.size() == 1);
    arrowlane::ChunkAssembler assembler;
    AL_CHECK(assembler.add(span_of(first_frames[0])).has_value());
    AL_CHECK_ERR(assembler.add(span_of(second_frames[0])), arrowlane::Code::Chunk);
}

AL_TEST(assembler_rejects_a_frame_after_last) {
    arrowlane::ChunkProducer producer(arrowlane::make_fixture(3000),
                                      arrowlane::LaneId::Ipc, 1000,
                                      arrowlane::kMaxPayloadBytes);
    const auto frames = take_frames(producer);
    AL_REQUIRE(frames.size() == 3);
    arrowlane::ChunkAssembler assembler;
    for (const auto& wire : frames) {
        AL_CHECK(assembler.add(span_of(wire)).has_value());
    }
    AL_CHECK(assembler.finished());
    AL_CHECK_ERR(assembler.add(span_of(frames[2])), arrowlane::Code::Chunk);
}

AL_TEST(assembler_rejects_a_short_stream) {
    arrowlane::ChunkProducer producer(arrowlane::make_fixture(3000),
                                      arrowlane::LaneId::Ipc, 1000,
                                      arrowlane::kMaxPayloadBytes);
    const auto frames = take_frames(producer);
    AL_REQUIRE(frames.size() == 3);
    arrowlane::ChunkAssembler assembler;
    AL_CHECK(assembler.add(span_of(frames[0])).has_value());
    AL_CHECK(assembler.add(span_of(frames[1])).has_value());
    AL_CHECK(!assembler.finished());
    AL_CHECK_ERR(assembler.check_complete(), arrowlane::Code::Chunk);
}

AL_TEST(assembler_rejects_a_frame_without_metadata) {
    const auto batch = arrowlane::make_fixture(3);
    auto frame = arrowlane::encode_lane(arrowlane::LaneId::Json, batch);
    AL_REQUIRE(frame.has_value());
    auto wire = arrowlane::encode_frame(*frame);
    AL_REQUIRE(wire.has_value());
    arrowlane::ChunkAssembler assembler;
    AL_CHECK_ERR(assembler.add(span_of(*wire)), arrowlane::Code::Chunk);
}
