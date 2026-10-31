// The in place encoder must produce exactly what the allocating one produces, and
// must refuse a buffer that is too small before writing anything.

#include "harness.hpp"

#include "arrowlane/fixture.hpp"
#include "arrowlane/ipc.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

AL_TEST(ipc_into_writes_the_same_bytes_as_the_allocating_encoder) {
    const auto batch = arrowlane::make_fixture(3);
    auto allocated = arrowlane::encode_ipc(batch, 0);
    AL_CHECK(allocated.has_value());

    std::vector<std::byte> buffer(allocated->size());
    auto written = arrowlane::encode_ipc_into(batch, buffer);
    AL_CHECK(written.has_value());
    AL_CHECK(written->size() == allocated->size());
    AL_CHECK(std::equal(written->begin(), written->end(), allocated->begin()));
}

AL_TEST(ipc_into_refuses_a_buffer_that_is_too_small) {
    const auto batch = arrowlane::make_fixture(3);
    auto allocated = arrowlane::encode_ipc(batch, 0);
    AL_CHECK(allocated.has_value());

    std::vector<std::byte> buffer(allocated->size() - 1);
    auto written = arrowlane::encode_ipc_into(batch, buffer);
    AL_CHECK(!written.has_value());
}

AL_TEST(ipc_into_writes_at_the_start_of_a_larger_buffer) {
    const auto batch = arrowlane::make_fixture(3);
    auto allocated = arrowlane::encode_ipc(batch, 0);
    AL_CHECK(allocated.has_value());

    // A caller that keeps one big buffer and hands the encoder a slice of it.
    std::vector<std::byte> buffer(allocated->size() * 2);
    auto written = arrowlane::encode_ipc_into(batch, buffer);
    AL_CHECK(written.has_value());
    AL_CHECK(written->data() == buffer.data());
    AL_CHECK(std::equal(written->begin(), written->end(), allocated->begin()));
}
