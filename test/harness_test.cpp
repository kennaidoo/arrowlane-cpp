#include "harness.hpp"

#include "arrowlane/error.hpp"

AL_TEST(error_to_string) {
    const auto error = arrowlane::make_error(arrowlane::Code::BadMagic, "test");
    AL_CHECK_EQ(error.to_string(), std::string("BadMagic: test"));
}

AL_TEST(expected_error_is_reported) {
    arrowlane::Result<int> value = std::unexpected(
        arrowlane::make_error(arrowlane::Code::TruncatedFrame, "short"));
    AL_CHECK_ERR(value, arrowlane::Code::TruncatedFrame);
    AL_CHECK_EQ(arrowlane::code_name(arrowlane::Code::Chunk), std::string("ChunkError"));
}
