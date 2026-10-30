#pragma once

#include <cstddef>
#include <string_view>

#include "arrowlane/column.hpp"
#include "arrowlane/error.hpp"
#include "arrowlane/frame.hpp"
#include "arrowlane/json.hpp"
#include "arrowlane/lanes.hpp"
#include "arrowlane/schema.hpp"

namespace arrowlane::detail {

[[nodiscard]] Result<JsonValue> lane_header(const Schema& schema,
                                            std::string_view media_type);
[[nodiscard]] Result<void> check_frame_schema(const JsonValue& header,
                                              const Schema& schema);

[[nodiscard]] Result<Frame> encode_json_lane(const RecordBatch& batch);
[[nodiscard]] Result<RecordBatch> decode_json_lane(const Frame& frame,
                                                   std::size_t max_uncompressed);

[[nodiscard]] Result<Frame> encode_ipc_lane(const RecordBatch& batch);
[[nodiscard]] Result<RecordBatch> decode_ipc_lane(const Frame& frame,
                                                  std::size_t max_uncompressed);

[[nodiscard]] Result<Frame> encode_ipc_zstd_lane(const RecordBatch& batch, int level);
[[nodiscard]] Result<RecordBatch> decode_ipc_zstd_lane(const Frame& frame,
                                                       std::size_t max_uncompressed);

[[nodiscard]] Result<Frame> encode_shm_lane(const RecordBatch& batch,
                                           const LaneOptions& options);
[[nodiscard]] Result<RecordBatch> decode_shm_lane(const Frame& frame,
                                                  const LaneOptions& options,
                                                  std::size_t max_uncompressed);

}  // namespace arrowlane::detail
