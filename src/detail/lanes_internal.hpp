#pragma once

#include <cstddef>

#include "arrowlane/column.hpp"
#include "arrowlane/error.hpp"
#include "arrowlane/frame.hpp"

namespace arrowlane::detail {

[[nodiscard]] Result<Frame> encode_json_lane(const RecordBatch& batch);
[[nodiscard]] Result<RecordBatch> decode_json_lane(const Frame& frame,
                                                   std::size_t max_uncompressed);

[[nodiscard]] Result<Frame> encode_ipc_lane(const RecordBatch& batch);
[[nodiscard]] Result<RecordBatch> decode_ipc_lane(const Frame& frame,
                                                  std::size_t max_uncompressed);

}  // namespace arrowlane::detail
