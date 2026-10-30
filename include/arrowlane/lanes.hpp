#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "arrowlane/column.hpp"
#include "arrowlane/error.hpp"
#include "arrowlane/frame.hpp"

namespace arrowlane {

struct LaneOptions {
    int level = -1;
    std::string token;
    std::string directory;
};

[[nodiscard]] Result<Frame> encode_lane(LaneId lane, const RecordBatch& batch,
                                        const LaneOptions& options = {});
[[nodiscard]] Result<RecordBatch> decode_lane(
    const Frame& frame, const LaneOptions& options = {},
    std::size_t max_uncompressed = kMaxUncompressedBytes);
[[nodiscard]] std::vector<std::string_view> lane_names();
[[nodiscard]] std::vector<std::string_view> available_lanes();

}  // namespace arrowlane
