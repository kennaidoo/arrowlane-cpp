#include "harness.hpp"

#include "arrowlane/buffer.hpp"

#include <cstddef>
#include <vector>

AL_TEST(buffer_owns_and_slices) {
    arrowlane::Buffer owned(
        std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}});
    AL_CHECK_EQ(owned.size(), std::size_t{3});
    AL_CHECK_EQ(owned.bytes()[1], std::byte{2});

    auto slice = arrowlane::buffer_slice(owned.owner(), 1, 2);
    AL_REQUIRE(slice.has_value());
    AL_CHECK_EQ(slice->size(), std::size_t{2});
    AL_CHECK_EQ((*slice)[0], std::byte{2});
    AL_CHECK(slice->bytes().data() == owned.bytes().data() + 1);
}

AL_TEST(buffer_keeps_the_owner_alive) {
    arrowlane::Buffer slice;
    {
        arrowlane::Buffer owned(std::vector<std::byte>{std::byte{9}, std::byte{8}});
        auto candidate = arrowlane::buffer_slice(owned.owner(), 0, 2);
        AL_REQUIRE(candidate.has_value());
        slice = *candidate;
    }
    AL_CHECK_EQ(slice.size(), std::size_t{2});
    AL_CHECK_EQ(slice.bytes()[0], std::byte{9});
}

AL_TEST(buffer_empty_slice_is_safe) {
    arrowlane::Buffer empty(std::vector<std::byte>{});
    auto slice = arrowlane::buffer_slice(empty.owner(), 0, 0);
    AL_REQUIRE(slice.has_value());
    AL_CHECK(slice->empty());
}

AL_TEST(buffer_rejects_bad_slices) {
    arrowlane::Buffer owned(std::vector<std::byte>{std::byte{1}});
    AL_CHECK_ERR(arrowlane::buffer_slice(owned.owner(), 2, 1),
                 arrowlane::Code::TruncatedFrame);
    AL_CHECK_ERR(arrowlane::buffer_slice(owned.owner(), 0, 2),
                 arrowlane::Code::TruncatedFrame);
    AL_CHECK_ERR(arrowlane::buffer_slice(nullptr, 0, 0), arrowlane::Code::Invalid);
}
