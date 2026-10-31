#include "arrowlane/lanes.hpp"

#include <string>
#include <string_view>
#include <vector>

#include "detail/lanes_internal.hpp"

namespace arrowlane {
Result<Frame> encode_lane(LaneId lane, const RecordBatch& batch,
                          const LaneOptions& options) {
    switch (lane) {
        case LaneId::Json: return detail::encode_json_lane(batch);
        case LaneId::Ipc: return detail::encode_ipc_lane(batch);
        case LaneId::IpcZstd:
            return detail::encode_ipc_zstd_lane(
                batch, options.level < 0 ? 3 : options.level);
        case LaneId::Shm: return detail::encode_shm_lane(batch, options);
    }
    return std::unexpected(make_error(Code::UnknownLane, "unknown lane"));
}

Result<RecordBatch> decode_lane(const Frame& frame, const LaneOptions& options,
                                std::size_t max_uncompressed) {
    if (frame.headroom != 0) {
        // A frame that came from encode_lane still holds the space reserved for
        // the prefix and the header. Hand the decoders a frame without it, so
        // no decoder needs to know about headroom.
        Frame body;
        body.lane = frame.lane;
        body.flags = frame.flags;
        body.rows = frame.rows;
        body.uncompressed_len = frame.uncompressed_len;
        body.header = frame.header;
        body.payload.assign(frame.body().begin(), frame.body().end());
        return decode_lane(body, options, max_uncompressed);
    }
    switch (frame.lane) {
        case LaneId::Json: return detail::decode_json_lane(frame, max_uncompressed);
        case LaneId::Ipc: return detail::decode_ipc_lane(frame, max_uncompressed);
        case LaneId::IpcZstd:
            return detail::decode_ipc_zstd_lane(frame, max_uncompressed);
        case LaneId::Shm:
            return detail::decode_shm_lane(frame, options, max_uncompressed);
    }
    return std::unexpected(make_error(Code::UnknownLane, "unknown lane"));
}

std::vector<std::string_view> lane_names() {
    return {"json", "ipc", "ipc-zstd", "shm"};
}

std::vector<std::string_view> available_lanes() {
#if defined(_WIN32)
    return {"json", "ipc", "ipc-zstd"};
#else
    // The reference lists a lane when the platform satisfies its requirement,
    // so shm is listed on POSIX even before a segment directory is probed.
    return {"json", "ipc", "ipc-zstd", "shm"};
#endif
}

}  // namespace arrowlane
