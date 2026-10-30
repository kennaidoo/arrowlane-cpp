#include "arrowlane/frame.hpp"

#include <cstring>
#include <string>
#include <utility>

#include "arrowlane/crc32.hpp"
#include "arrowlane/version.hpp"
#include "detail/bytes.hpp"

namespace arrowlane {

std::optional<LaneId> lane_id_from_name(std::string_view name) noexcept {
    if (name == "json") return LaneId::Json;
    if (name == "ipc") return LaneId::Ipc;
    if (name == "ipc-zstd") return LaneId::IpcZstd;
    if (name == "shm") return LaneId::Shm;
    return std::nullopt;
}

std::string_view lane_name(LaneId lane) noexcept {
    switch (lane) {
        case LaneId::Json: return "json";
        case LaneId::Ipc: return "ipc";
        case LaneId::IpcZstd: return "ipc-zstd";
        case LaneId::Shm: return "shm";
    }
    return "";
}

Result<void> validate_chunk(const ChunkInfo& chunk) {
    if (chunk.id.size() != 32) {
        return std::unexpected(
            make_error(Code::Chunk, "chunk id must be 32 lowercase hex characters"));
    }
    for (const char c : chunk.id) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) {
            return std::unexpected(
                make_error(Code::Chunk, "chunk id must be 32 lowercase hex characters"));
        }
    }
    if (chunk.index >= kMaxChunks) {
        return std::unexpected(make_error(
            Code::Chunk, "chunk index must be 0.." + std::to_string(kMaxChunks - 1)));
    }
    return {};
}

Result<ChunkInfo> parse_chunk(const JsonValue& value) {
    if (!value.is_object()) {
        return std::unexpected(make_error(Code::Chunk, "chunk header is not an object"));
    }
    if (value.fields().size() != 3) {
        return std::unexpected(
            make_error(Code::Chunk, "chunk header must carry id, index and last"));
    }
    const JsonValue* id = value.find("id");
    const JsonValue* index = value.find("index");
    const JsonValue* last = value.find("last");
    if (id == nullptr || index == nullptr || last == nullptr) {
        return std::unexpected(
            make_error(Code::Chunk, "chunk header must carry id, index and last"));
    }
    if (id->kind() != JsonValue::Kind::String || index->kind() != JsonValue::Kind::Number ||
        !index->is_integer() || last->kind() != JsonValue::Kind::Bool) {
        return std::unexpected(
            make_error(Code::Chunk, "chunk header has the wrong field types"));
    }
    ChunkInfo chunk;
    chunk.id = id->as_string();
    chunk.index = static_cast<std::uint64_t>(index->as_int());
    chunk.last = last->as_bool();
    auto valid = validate_chunk(chunk);
    if (!valid) return std::unexpected(valid.error());
    return chunk;
}

std::optional<ChunkInfo> Frame::chunk() const {
    const JsonValue* value = header.find("chunk");
    if (value == nullptr) return std::nullopt;
    auto parsed = parse_chunk(*value);
    if (!parsed) return std::nullopt;
    return *parsed;
}

namespace {

struct Prefix {
    std::uint8_t lane_id = 0;
    std::uint16_t flags = 0;
    std::uint64_t payload_len = 0;
    std::uint64_t uncompressed_len = 0;
    std::uint64_t rows = 0;
    std::uint32_t crc = 0;
    std::uint16_t header_len = 0;
};

Result<Prefix> read_prefix(std::span<const std::byte> data, std::size_t max_payload,
                           std::size_t max_uncompressed) {
    if (data.size() < kPrefixSize) {
        return std::unexpected(make_error(
            Code::TruncatedFrame,
            std::to_string(data.size()) + " bytes is shorter than " +
                std::to_string(kPrefixSize)));
    }
    if (std::memcmp(data.data(), "ALN1", 4) != 0) {
        return std::unexpected(make_error(Code::BadMagic, "expected ALN1"));
    }
    if (static_cast<std::uint8_t>(data[4]) != kFormatVersion) {
        return std::unexpected(make_error(
            Code::UnsupportedVersion,
            "frame version " + std::to_string(static_cast<unsigned>(data[4])) +
                ", expected " + std::to_string(kFormatVersion)));
    }
    Prefix prefix;
    prefix.lane_id = static_cast<std::uint8_t>(data[5]);
    if (lane_name(static_cast<LaneId>(prefix.lane_id)).empty()) {
        return std::unexpected(make_error(
            Code::UnknownLane,
            "lane id " + std::to_string(prefix.lane_id) + " is not registered"));
    }
    prefix.flags = detail::load_le<std::uint16_t>(data.data() + 6);
    prefix.payload_len = detail::load_le<std::uint64_t>(data.data() + 8);
    prefix.uncompressed_len = detail::load_le<std::uint64_t>(data.data() + 16);
    prefix.rows = detail::load_le<std::uint64_t>(data.data() + 24);
    prefix.crc = detail::load_le<std::uint32_t>(data.data() + 32);
    prefix.header_len = detail::load_le<std::uint16_t>(data.data() + 36);
    if (prefix.header_len > kMaxHeaderBytes) {
        return std::unexpected(make_error(
            Code::OversizeFrame,
            "header " + std::to_string(prefix.header_len) + " exceeds " +
                std::to_string(kMaxHeaderBytes)));
    }
    if (prefix.payload_len > kMaxPayloadBytes || prefix.payload_len > max_payload) {
        return std::unexpected(make_error(
            Code::OversizeFrame,
            "payload " + std::to_string(prefix.payload_len) + " exceeds " +
                std::to_string(max_payload)));
    }
    if (prefix.uncompressed_len > kMaxUncompressedBytes ||
        prefix.uncompressed_len > max_uncompressed) {
        return std::unexpected(make_error(
            Code::OversizeFrame,
            "uncompressed length " + std::to_string(prefix.uncompressed_len) +
                " exceeds " + std::to_string(max_uncompressed)));
    }
    return prefix;
}

Result<bool> read_exact(std::istream& stream, std::span<std::byte> out) {
    if (out.empty()) return true;
    stream.read(reinterpret_cast<char*>(out.data()),
                static_cast<std::streamsize>(out.size()));
    const std::streamsize got = stream.gcount();
    if (got == 0) return false;
    if (got < static_cast<std::streamsize>(out.size())) {
        return std::unexpected(make_error(
            Code::TruncatedFrame,
            "stream ended after " + std::to_string(got) + " of " +
                std::to_string(out.size()) + " bytes"));
    }
    return true;
}

}  // namespace

Result<FrameParts> encode_parts(const Frame& frame, std::size_t max_payload) {
    if (max_payload > kMaxPayloadBytes) {
        return std::unexpected(make_error(
            Code::OversizeFrame,
            "max_payload " + std::to_string(max_payload) + " exceeds " +
                std::to_string(kMaxPayloadBytes)));
    }
    if (frame.payload.size() > max_payload) {
        return std::unexpected(make_error(
            Code::OversizeFrame,
            "payload " + std::to_string(frame.payload.size()) + " exceeds " +
                std::to_string(max_payload)));
    }
    if (!frame.header.is_object()) {
        return std::unexpected(make_error(Code::BadHeader, "header is not a JSON object"));
    }
    JsonValue header = frame.header;
    if (header.find("producer") == nullptr) {
        header.set("producer", JsonValue(std::string(kProducer)));
    }
    FrameParts parts;
    parts.header = json_write_sorted(header);
    if (parts.header.size() > kMaxHeaderBytes) {
        return std::unexpected(make_error(
            Code::OversizeFrame,
            "header " + std::to_string(parts.header.size()) + " exceeds " +
                std::to_string(kMaxHeaderBytes)));
    }
    const std::uint32_t checksum = crc32(frame.payload);
    std::byte* prefix = parts.prefix.data();
    std::memcpy(prefix, "ALN1", 4);
    prefix[4] = static_cast<std::byte>(kFormatVersion);
    prefix[5] = static_cast<std::byte>(frame.lane);
    detail::store_le<std::uint16_t>(prefix + 6, frame.flags);
    detail::store_le<std::uint64_t>(prefix + 8,
                                    static_cast<std::uint64_t>(frame.payload.size()));
    detail::store_le<std::uint64_t>(prefix + 16, frame.uncompressed_len);
    detail::store_le<std::uint64_t>(prefix + 24, frame.rows);
    detail::store_le<std::uint32_t>(prefix + 32, checksum);
    detail::store_le<std::uint16_t>(prefix + 36,
                                    static_cast<std::uint16_t>(parts.header.size()));
    parts.payload = frame.payload;
    return parts;
}

Result<std::vector<std::byte>> encode_frame(const Frame& frame, std::size_t max_payload) {
    auto parts = encode_parts(frame, max_payload);
    if (!parts) return std::unexpected(parts.error());
    std::vector<std::byte> out(kPrefixSize + parts->header.size() + parts->payload.size());
    std::memcpy(out.data(), parts->prefix.data(), kPrefixSize);
    std::memcpy(out.data() + kPrefixSize, parts->header.data(), parts->header.size());
    if (!parts->payload.empty()) {
        std::memcpy(out.data() + kPrefixSize + parts->header.size(), parts->payload.data(),
                    parts->payload.size());
    }
    return out;
}

namespace {

Result<Frame> decode_frame_parts(std::span<const std::byte> prefix_bytes,
                                 std::span<const std::byte> header_bytes,
                                 std::span<const std::byte> payload,
                                 std::size_t max_payload, std::size_t max_uncompressed) {
    auto prefix = read_prefix(prefix_bytes, max_payload, max_uncompressed);
    if (!prefix) return std::unexpected(prefix.error());
    if (static_cast<std::uint64_t>(header_bytes.size()) != prefix->header_len) {
        return std::unexpected(make_error(
            Code::TruncatedFrame,
            "header declares " + std::to_string(prefix->header_len) + " bytes, carries " +
                std::to_string(header_bytes.size())));
    }
    if (static_cast<std::uint64_t>(payload.size()) != prefix->payload_len) {
        return std::unexpected(make_error(
            Code::TruncatedFrame,
            "payload declares " + std::to_string(prefix->payload_len) + " bytes, carries " +
                std::to_string(payload.size())));
    }
    auto header = json_parse(std::string_view(
        reinterpret_cast<const char*>(header_bytes.data()), header_bytes.size()));
    if (!header) return std::unexpected(header.error());
    if (!header->is_object()) {
        return std::unexpected(make_error(Code::BadHeader, "header is not a JSON object"));
    }
    if (const JsonValue* chunk_value = header->find("chunk"); chunk_value != nullptr) {
        auto chunk = parse_chunk(*chunk_value);
        if (!chunk) {
            return std::unexpected(make_error(
                Code::BadHeader,
                std::string("chunk metadata is invalid: ") + chunk.error().message));
        }
    }
    if (crc32(payload) != prefix->crc) {
        return std::unexpected(make_error(Code::ChecksumMismatch,
                                          "payload does not match the frame CRC"));
    }
    Frame frame;
    frame.lane = static_cast<LaneId>(prefix->lane_id);
    frame.payload.assign(payload.begin(), payload.end());
    frame.rows = prefix->rows;
    frame.uncompressed_len = prefix->uncompressed_len;
    frame.flags = prefix->flags;
    frame.header = std::move(*header);
    return frame;
}

}  // namespace

Result<Frame> decode_frame(std::span<const std::byte> data, std::size_t max_payload,
                           std::size_t max_uncompressed) {
    auto prefix = read_prefix(data, max_payload, max_uncompressed);
    if (!prefix) return std::unexpected(prefix.error());
    const std::size_t body_start = kPrefixSize + prefix->header_len;
    const std::size_t declared = body_start + static_cast<std::size_t>(prefix->payload_len);
    if (declared != data.size()) {
        return std::unexpected(make_error(
            Code::TruncatedFrame,
            "frame declares " + std::to_string(declared) + " bytes, carries " +
                std::to_string(data.size())));
    }
    return decode_frame_parts(data.first(kPrefixSize),
                              data.subspan(kPrefixSize, prefix->header_len),
                              data.subspan(body_start), max_payload, max_uncompressed);
}

Result<std::uint64_t> write_frame(std::ostream& stream, const Frame& frame,
                                  std::size_t max_payload) {
    auto parts = encode_parts(frame, max_payload);
    if (!parts) return std::unexpected(parts.error());
    stream.write(reinterpret_cast<const char*>(parts->prefix.data()),
                 static_cast<std::streamsize>(parts->prefix.size()));
    stream.write(parts->header.data(), static_cast<std::streamsize>(parts->header.size()));
    if (!parts->payload.empty()) {
        stream.write(reinterpret_cast<const char*>(parts->payload.data()),
                     static_cast<std::streamsize>(parts->payload.size()));
    }
    if (!stream) {
        return std::unexpected(make_error(Code::Io, "stream write failed"));
    }
    return static_cast<std::uint64_t>(parts->prefix.size() + parts->header.size() +
                                      parts->payload.size());
}

Result<std::optional<Frame>> read_frame(std::istream& stream, std::size_t max_payload,
                                        std::size_t max_uncompressed) {
    std::array<std::byte, kPrefixSize> prefix{};
    auto status = read_exact(stream, prefix);
    if (!status) return std::unexpected(status.error());
    if (!*status) return std::optional<Frame>{};
    auto fields = read_prefix(prefix, max_payload, max_uncompressed);
    if (!fields) return std::unexpected(fields.error());
    std::vector<std::byte> header(static_cast<std::size_t>(fields->header_len));
    auto header_status = read_exact(stream, header);
    if (!header_status) return std::unexpected(header_status.error());
    if (!*header_status) {
        return std::unexpected(make_error(Code::TruncatedFrame, "stream ended inside a frame"));
    }
    std::vector<std::byte> payload(static_cast<std::size_t>(fields->payload_len));
    auto payload_status = read_exact(stream, payload);
    if (!payload_status) return std::unexpected(payload_status.error());
    if (!*payload_status) {
        return std::unexpected(make_error(Code::TruncatedFrame, "stream ended inside a frame"));
    }
    auto frame = decode_frame_parts(prefix, header, payload, max_payload, max_uncompressed);
    if (!frame) return std::unexpected(frame.error());
    return std::optional<Frame>{std::move(*frame)};
}

}  // namespace arrowlane
