#include "arrowlane/lanes.hpp"

#include <string>
#include <string_view>
#include <vector>

#include "detail/lanes_internal.hpp"

namespace arrowlane {
namespace {

Error unavailable(LaneId lane) {
    return make_error(Code::LaneUnavailable,
                      std::string("lane ") + std::string(lane_name(lane)) +
                          " is not available in this build");
}

}  // namespace

Result<Frame> encode_lane(LaneId lane, const RecordBatch& batch,
                          const LaneOptions& options) {
    switch (lane) {
        case LaneId::Json: return detail::encode_json_lane(batch);
        case LaneId::Ipc: return detail::encode_ipc_lane(batch);
        case LaneId::IpcZstd:
            return detail::encode_ipc_zstd_lane(
                batch, options.level < 0 ? 3 : options.level);
        case LaneId::Shm: return std::unexpected(unavailable(lane));
    }
    return std::unexpected(make_error(Code::UnknownLane, "unknown lane"));
}

Result<RecordBatch> decode_lane(const Frame& frame, const LaneOptions& /*options*/,
                                std::size_t max_uncompressed) {
    switch (frame.lane) {
        case LaneId::Json: return detail::decode_json_lane(frame, max_uncompressed);
        case LaneId::Ipc: return detail::decode_ipc_lane(frame, max_uncompressed);
        case LaneId::IpcZstd:
            return detail::decode_ipc_zstd_lane(frame, max_uncompressed);
        case LaneId::Shm: return std::unexpected(unavailable(frame.lane));
    }
    return std::unexpected(make_error(Code::UnknownLane, "unknown lane"));
}

std::vector<std::string_view> lane_names() {
    return {"json", "ipc", "ipc-zstd", "shm"};
}

std::vector<std::string_view> available_lanes() {
    return {"json", "ipc", "ipc-zstd"};
}

}  // namespace arrowlane
