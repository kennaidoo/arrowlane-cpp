// Allocation budgets. The counting operator new below is global to this test
// binary only, so it measures exactly what these cases do.
//
// The procedure is the one in the plan: run once, read the counts, pin them.
// A later change that raises a budget has to say why in the commit message.

#include "harness.hpp"

#include "arrowlane/column.hpp"
#include "arrowlane/fixture.hpp"
#include "arrowlane/frame.hpp"
#include "arrowlane/ipc.hpp"
#include "arrowlane/lanes.hpp"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <span>
#include <vector>

namespace {

std::atomic<std::size_t> g_allocations{0};
std::atomic<std::size_t> g_bytes{0};

struct Counter {
    std::size_t allocations;
    std::size_t bytes;
};

Counter read_counter() {
    return Counter{g_allocations.load(std::memory_order_relaxed),
                   g_bytes.load(std::memory_order_relaxed)};
}

void reset_counter() {
    g_allocations.store(0, std::memory_order_relaxed);
    g_bytes.store(0, std::memory_order_relaxed);
}

}  // namespace

void* operator new(std::size_t size) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(size, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) return pointer;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

AL_TEST(alloc_json_encode_is_bounded) {
    const auto batch = arrowlane::make_fixture(1000);
    auto frame = arrowlane::encode_lane(arrowlane::LaneId::Json, batch);
    AL_REQUIRE(frame.has_value());
    reset_counter();
    auto encoded = arrowlane::encode_lane(arrowlane::LaneId::Json, batch);
    const Counter counter = read_counter();
    AL_REQUIRE(encoded.has_value());
    std::printf("BUDGET json_encode allocations=%zu bytes=%zu\n", counter.allocations,
                counter.bytes);
    // Measured 2026-10-30: 309673 allocations for 1000 rows, which is about
    // 310 per row. That breaks the plan's rule that json encode makes no per
    // row allocation, and it is the reason the json lane costs 8 microseconds
    // per row. The budget pins the measured number so it cannot get worse; the
    // open task is to emit rows into one reusable buffer.
    AL_CHECK(counter.allocations <= 320000);
}

AL_TEST(alloc_ipc_encode_is_bounded) {
    const auto batch = arrowlane::make_fixture(1000);
    auto first = arrowlane::encode_lane(arrowlane::LaneId::Ipc, batch);
    AL_REQUIRE(first.has_value());
    reset_counter();
    auto encoded = arrowlane::encode_lane(arrowlane::LaneId::Ipc, batch);
    const Counter counter = read_counter();
    AL_REQUIRE(encoded.has_value());
    std::printf("BUDGET ipc_encode allocations=%zu bytes=%zu\n", counter.allocations,
                counter.bytes);
    // The body is written straight into the wire buffer, so the encoder
    // allocates about one payload plus the messages. Measured 2026-10-30:
    // 80730 bytes for a 63001 byte payload, down from 141993 when the body had
    // its own vector. The budget is one payload plus 24 KB of fixed overhead,
    // which scales with the table instead of pinning a magic number.
    AL_CHECK(counter.allocations <= 400);
    AL_CHECK(counter.bytes <= encoded->payload.size() + 24000);
}

AL_TEST(alloc_ipc_view_decode_copies_nothing) {
    const auto batch = arrowlane::make_fixture(1000);
    auto frame = arrowlane::encode_lane(arrowlane::LaneId::Ipc, batch);
    AL_REQUIRE(frame.has_value());
    auto wire = arrowlane::encode_frame(*frame);
    AL_REQUIRE(wire.has_value());
    auto owner = std::make_shared<const std::vector<std::byte>>(*wire);

    reset_counter();
    auto view = arrowlane::decode_frame_view(
        std::span<const std::byte>(owner->data(), owner->size()));
    AL_REQUIRE(view.has_value());
    auto decoded = arrowlane::unpack_view(*view, owner);
    const Counter counter = read_counter();
    AL_REQUIRE(decoded.has_value());
    std::printf("BUDGET ipc_view_decode allocations=%zu bytes=%zu\n", counter.allocations,
                counter.bytes);
    // The whole point of the view path: no copy of the payload. The column
    // structs themselves are a handful of allocations.
    AL_CHECK(counter.bytes < owner->size() / 4);
}

AL_TEST(alloc_owning_decode_copies_the_payload_once) {
    const auto batch = arrowlane::make_fixture(1000);
    auto frame = arrowlane::encode_lane(arrowlane::LaneId::Ipc, batch);
    AL_REQUIRE(frame.has_value());
    auto wire = arrowlane::encode_frame(*frame);
    AL_REQUIRE(wire.has_value());

    reset_counter();
    auto decoded = arrowlane::decode_frame(
        std::span<const std::byte>(wire->data(), wire->size()));
    AL_REQUIRE(decoded.has_value());
    auto out = arrowlane::decode_lane(*decoded);
    const Counter counter = read_counter();
    AL_REQUIRE(out.has_value());
    std::printf("BUDGET ipc_owning_decode allocations=%zu bytes=%zu payload=%zu\n",
                counter.allocations, counter.bytes, wire->size());
    // The owning path copies the payload once into the frame and then slices
    // the columns out of it. Twice the payload plus slack is the budget.
    AL_CHECK(counter.bytes < wire->size() * 3);
}
