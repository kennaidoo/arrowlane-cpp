#include "arrowlane/lanes.hpp"

#include <string>
#include <utility>

#include "arrowlane/base64.hpp"
#include "arrowlane/ipc.hpp"
#include "arrowlane/json.hpp"
#include "arrowlane/schema.hpp"
#include "arrowlane/version.hpp"
#include "detail/lanes_internal.hpp"

namespace arrowlane {

Result<JsonValue> detail::lane_header(const Schema& schema,
                                      std::string_view media_type) {
    auto message = encode_schema_message(schema);
    if (!message) return std::unexpected(message.error());
    JsonValue header = JsonValue::make_object();
    header.set("schema", JsonValue(base64_encode(*message)));
    header.set("ncols", JsonValue(static_cast<std::int64_t>(schema.fields.size())));
    header.set("producer", JsonValue(std::string("arrowlane-cpp/") + kVersion));
    header.set("media_type", JsonValue(std::string(media_type)));
    return header;
}

Result<void> detail::check_frame_schema(const JsonValue& header,
                                        const Schema& schema) {
    const JsonValue* schema_value = header.find("schema");
    if (!schema_value || schema_value->kind() != JsonValue::Kind::String) {
        return std::unexpected(make_error(Code::BadHeader, "header carries no schema"));
    }
    auto bytes = base64_decode(schema_value->as_string());
    if (!bytes) {
        return std::unexpected(
            make_error(Code::BadHeader, "schema is not valid base64"));
    }
    auto header_schema = decode_schema_message(*bytes);
    if (!header_schema) return std::unexpected(header_schema.error());
    if (!(*header_schema == schema)) {
        return std::unexpected(make_error(
            Code::Frame, "frame header schema does not match the payload schema"));
    }
    return {};
}

Result<Frame> detail::encode_ipc_lane(const RecordBatch& batch) {
    auto valid = validate_batch(batch);
    if (!valid) return std::unexpected(valid.error());
    auto header = detail::lane_header(batch.schema,
                                      "application/vnd.apache.arrow.stream");
    if (!header) return std::unexpected(header.error());
    // Reserve the prefix and the header in front of the stream, so encode_frame
    // can write the wire in this buffer instead of copying the payload.
    const auto header_text = json_write_sorted(*header);
    const std::size_t headroom = kPrefixSize + header_text.size();
    auto raw = encode_ipc(batch, headroom);
    if (!raw) return std::unexpected(raw.error());

    Frame frame;
    frame.lane = LaneId::Ipc;
    frame.headroom = headroom;
    frame.payload = std::move(*raw);
    frame.rows = static_cast<std::uint64_t>(batch.rows());
    frame.uncompressed_len = frame.payload.size() - headroom;
    frame.flags = 0;
    frame.header = std::move(*header);
    return frame;
}

Result<RecordBatch> detail::decode_ipc_lane(const Frame& frame,
                                            std::size_t max_uncompressed) {
    if (frame.uncompressed_len != frame.payload.size()) {
        return std::unexpected(make_error(
            Code::Frame, "frame declares a different uncompressed length"));
    }
    if (frame.payload.size() > max_uncompressed) {
        return std::unexpected(make_error(
            Code::OversizeFrame, "payload " + std::to_string(frame.payload.size()) +
                                     " exceeds " + std::to_string(max_uncompressed)));
    }
    auto batch = decode_ipc(frame.payload, max_uncompressed);
    if (!batch) return std::unexpected(batch.error());
    auto header_ok = detail::check_frame_schema(frame.header, batch->schema);
    if (!header_ok) return std::unexpected(header_ok.error());
    if (static_cast<std::uint64_t>(batch->rows()) != frame.rows) {
        return std::unexpected(
            make_error(Code::Frame, "frame rows do not match the payload"));
    }
    return batch;
}

Result<RecordBatch> unpack_view(const FrameView& view,
                                std::shared_ptr<const std::vector<std::byte>> owner,
                                const LaneOptions& /*options*/,
                                std::size_t max_uncompressed) {
    if (view.lane != LaneId::Ipc) {
        return std::unexpected(make_error(
            Code::Unsupported, "the view path supports the ipc lane only"));
    }
    if (view.uncompressed_len != view.payload.size()) {
        return std::unexpected(make_error(
            Code::Frame, "frame declares a different uncompressed length"));
    }
    if (view.payload.size() > max_uncompressed) {
        return std::unexpected(make_error(
            Code::OversizeFrame, "payload " + std::to_string(view.payload.size()) +
                                     " exceeds " + std::to_string(max_uncompressed)));
    }
    if (!owner) {
        return std::unexpected(make_error(Code::Invalid, "buffer owner is null"));
    }
    const auto* start = owner->data();
    const auto* end = start + owner->size();
    if (view.payload.data() < start || view.payload.data() > end ||
        view.payload.size() > static_cast<std::size_t>(end - view.payload.data())) {
        return std::unexpected(
            make_error(Code::Invalid, "payload does not lie inside the owner"));
    }
    const auto base = static_cast<std::size_t>(view.payload.data() - start);
    auto batch = decode_ipc_view(view.payload, owner, base, max_uncompressed);
    if (!batch) return std::unexpected(batch.error());
    auto header_ok = detail::check_frame_schema(view.header, batch->schema);
    if (!header_ok) return std::unexpected(header_ok.error());
    if (static_cast<std::uint64_t>(batch->rows()) != view.rows) {
        return std::unexpected(
            make_error(Code::Frame, "frame rows do not match the payload"));
    }
    return batch;
}

}  // namespace arrowlane
