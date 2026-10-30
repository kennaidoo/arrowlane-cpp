#include "arrowlane/error.hpp"

namespace arrowlane {

std::string Error::to_string() const {
    return std::string(code_name(code)) + ": " + message;
}

const char* code_name(Code code) noexcept {
    switch (code) {
        case Code::Config: return "ConfigError";
        case Code::Frame: return "FrameError";
        case Code::BadMagic: return "BadMagic";
        case Code::UnsupportedVersion: return "UnsupportedVersion";
        case Code::TruncatedFrame: return "TruncatedFrame";
        case Code::OversizeFrame: return "OversizeFrame";
        case Code::ChecksumMismatch: return "ChecksumMismatch";
        case Code::UnknownLane: return "UnknownLane";
        case Code::BadHeader: return "BadHeader";
        case Code::Chunk: return "ChunkError";
        case Code::LaneUnavailable: return "LaneUnavailable";
        case Code::Segment: return "SegmentError";
        case Code::SegmentGone: return "SegmentGone";
        case Code::Auth: return "AuthError";
        case Code::Limit: return "LimitExceeded";
        case Code::Upstream: return "UpstreamError";
        case Code::Unsupported: return "UnsupportedType";
        case Code::Io: return "IoError";
        case Code::Invalid: return "InvalidArgument";
    }
    return "ArrowLaneError";
}

Error make_error(Code code, std::string message) { return Error{code, std::move(message)}; }

}  // namespace arrowlane
