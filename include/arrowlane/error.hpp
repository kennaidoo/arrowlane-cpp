#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace arrowlane {

enum class Code {
    Config,
    Frame,
    BadMagic,
    UnsupportedVersion,
    TruncatedFrame,
    OversizeFrame,
    ChecksumMismatch,
    UnknownLane,
    BadHeader,
    Chunk,
    LaneUnavailable,
    Segment,
    SegmentGone,
    Auth,
    Limit,
    Upstream,
    Unsupported,
    Io,
    Invalid,
};

struct Error {
    Code code = Code::Frame;
    std::string message;

    [[nodiscard]] std::string to_string() const;
};

template <class T>
using Result = std::expected<T, Error>;

[[nodiscard]] const char* code_name(Code code) noexcept;

[[nodiscard]] Error make_error(Code code, std::string message);

}  // namespace arrowlane
