#include "arrowlane/lanes.hpp"

#include <string>
#include <utility>
#include <vector>

#include "arrowlane/ipc.hpp"
#include "arrowlane/shm.hpp"
#include "detail/lanes_internal.hpp"

namespace arrowlane {

Result<Frame> detail::encode_shm_lane(const RecordBatch& batch,
                                      const LaneOptions& options) {
    if (!is_ready(options.directory)) {
        return std::unexpected(make_error(
            Code::LaneUnavailable,
            "the shm lane needs a writable memory backed segment directory"));
    }
    if (options.token.empty()) {
        return std::unexpected(
            make_error(Code::LaneUnavailable, "the shm lane requires a shared token"));
    }
    auto valid = validate_batch(batch);
    if (!valid) return std::unexpected(valid.error());
    auto raw = encode_ipc(batch);
    if (!raw) return std::unexpected(raw.error());
    auto descriptor = write_segment(*raw, options.directory, options.token);
    if (!descriptor) return std::unexpected(descriptor.error());
    auto header = detail::lane_header(batch.schema, "application/vnd.arrowlane.shm");
    if (!header) return std::unexpected(header.error());
    header->set("segment", JsonValue(descriptor->name));

    const std::string payload = descriptor->to_json();
    Frame frame;
    frame.lane = LaneId::Shm;
    frame.payload.reserve(payload.size());
    for (const char character : payload) {
        frame.payload.push_back(static_cast<std::byte>(character));
    }
    frame.rows = static_cast<std::uint64_t>(batch.rows());
    frame.uncompressed_len = raw->size();
    frame.flags = 0;
    frame.header = std::move(*header);
    return frame;
}

Result<RecordBatch> detail::decode_shm_lane(const Frame& frame,
                                            const LaneOptions& options,
                                            std::size_t max_uncompressed) {
    if (options.token.empty()) {
        return std::unexpected(
            make_error(Code::LaneUnavailable, "the shm lane requires a shared token"));
    }
    const std::string text(reinterpret_cast<const char*>(frame.payload.data()),
                           frame.payload.size());
    auto descriptor = ShmDescriptor::from_json(text);
    if (!descriptor) return std::unexpected(descriptor.error());
    if (descriptor->length != frame.uncompressed_len) {
        return std::unexpected(make_error(
            Code::Segment, "descriptor length does not match the frame"));
    }
    if (descriptor->length > max_uncompressed) {
        return std::unexpected(make_error(Code::OversizeFrame,
                                          "segment exceeds the uncompressed cap"));
    }
    auto bytes = read_segment(*descriptor, options.directory, options.token);
    if (!bytes) return std::unexpected(bytes.error());
    auto batch = decode_ipc(*bytes, max_uncompressed);
    if (!batch) return std::unexpected(batch.error());
    auto header_ok = detail::check_frame_schema(frame.header, batch->schema);
    if (!header_ok) return std::unexpected(header_ok.error());
    if (static_cast<std::uint64_t>(batch->rows()) != frame.rows) {
        return std::unexpected(
            make_error(Code::Frame, "frame rows do not match the payload"));
    }
    return batch;
}

}  // namespace arrowlane
