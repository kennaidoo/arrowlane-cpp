#include "arrowlane/lanes.hpp"

#include <string>
#include <utility>
#include <vector>

#if defined(AL_HAVE_ZSTD)
#include <zstd.h>
#endif

#include "arrowlane/ipc.hpp"
#include "arrowlane/json.hpp"
#include "arrowlane/schema.hpp"
#include "detail/lanes_internal.hpp"

namespace arrowlane {

#if defined(AL_HAVE_ZSTD)

Result<Frame> detail::encode_ipc_zstd_lane(const RecordBatch& batch, int level) {
    if (level < 1 || level > 19) {
        return std::unexpected(
            make_error(Code::Invalid, "zstd level must be 1..19"));
    }
    auto raw = encode_ipc(batch);
    if (!raw) return std::unexpected(raw.error());

    std::vector<std::byte> payload(ZSTD_compressBound(raw->size()));
    const std::size_t written =
        ZSTD_compress(payload.data(), payload.size(), raw->data(), raw->size(), level);
    if (ZSTD_isError(written)) {
        return std::unexpected(make_error(
            Code::Frame, std::string("zstd compression failed: ") +
                             ZSTD_getErrorName(written)));
    }
    payload.resize(written);

    auto header = detail::lane_header(batch.schema,
                                      "application/vnd.apache.arrow.stream");
    if (!header) return std::unexpected(header.error());
    header->set("codec", JsonValue(std::string("zstd")));
    header->set("zstd_level", JsonValue(static_cast<std::int64_t>(level)));

    Frame frame;
    frame.lane = LaneId::IpcZstd;
    frame.payload = std::move(payload);
    frame.rows = static_cast<std::uint64_t>(batch.rows());
    frame.uncompressed_len = raw->size();
    frame.flags = 0;
    frame.header = std::move(*header);
    return frame;
}

Result<RecordBatch> detail::decode_ipc_zstd_lane(const Frame& frame,
                                                 std::size_t max_uncompressed) {
    if (frame.uncompressed_len > max_uncompressed) {
        return std::unexpected(make_error(
            Code::OversizeFrame, "frame declares " +
                                     std::to_string(frame.uncompressed_len) +
                                     " uncompressed bytes, cap is " +
                                     std::to_string(max_uncompressed)));
    }
    if (frame.payload.size() > max_uncompressed) {
        return std::unexpected(make_error(
            Code::OversizeFrame, "payload " + std::to_string(frame.payload.size()) +
                                     " exceeds " + std::to_string(max_uncompressed)));
    }
    std::vector<std::byte> raw(static_cast<std::size_t>(frame.uncompressed_len));
    const std::size_t written =
        ZSTD_decompress(raw.data(), raw.size(), frame.payload.data(),
                        frame.payload.size());
    if (ZSTD_isError(written)) {
        return std::unexpected(make_error(
            Code::Frame, std::string("zstd decompression failed: ") +
                             ZSTD_getErrorName(written)));
    }
    if (written != raw.size()) {
        return std::unexpected(make_error(
            Code::Frame, "zstd wrote " + std::to_string(written) + " bytes, expected " +
                             std::to_string(raw.size())));
    }
    auto batch = decode_ipc(raw, max_uncompressed);
    if (!batch) return std::unexpected(batch.error());
    auto header_ok = detail::check_frame_schema(frame.header, batch->schema);
    if (!header_ok) return std::unexpected(header_ok.error());
    if (static_cast<std::uint64_t>(batch->rows()) != frame.rows) {
        return std::unexpected(
            make_error(Code::Frame, "frame rows do not match the payload"));
    }
    return batch;
}

#else

Result<Frame> detail::encode_ipc_zstd_lane(const RecordBatch&, int) {
    return std::unexpected(make_error(Code::LaneUnavailable,
                                      "this build has no zstd support"));
}

Result<RecordBatch> detail::decode_ipc_zstd_lane(const Frame&, std::size_t) {
    return std::unexpected(make_error(Code::LaneUnavailable,
                                      "this build has no zstd support"));
}

#endif

}  // namespace arrowlane
