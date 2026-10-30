#include "arrowlane/chunks.hpp"

#include <algorithm>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "arrowlane/json.hpp"
#include "arrowlane/schema.hpp"
#include "detail/bytes.hpp"

namespace arrowlane {
namespace {

Error chunk_error(std::string message) {
    return make_error(Code::Chunk, std::move(message));
}

std::size_t offset_in_owner(const Buffer& buffer) {
    if (buffer.empty()) return 0;
    const auto* base = buffer.owner()->data();
    return static_cast<std::size_t>(buffer.bytes().data() - base);
}

// 32 lowercase hex characters. Half comes from the device, half from a counter,
// so two producers in one process never share an id.
std::string make_stream_id() {
    static const std::uint64_t seed = [] {
        std::random_device device;
        const std::uint64_t high = static_cast<std::uint64_t>(device()) << 32;
        return high ^ static_cast<std::uint64_t>(device());
    }();
    static std::uint64_t counter = 0;
    ++counter;
    char text[33];
    // 32 nibbles: 16 from the device seed, 16 from the counter.
    for (int index = 0; index < 32; ++index) {
        const std::uint64_t source = index < 16 ? seed : counter;
        const int shift = 60 - 4 * (index % 16);
        const int nibble = static_cast<int>((source >> shift) & 0x0F);
        text[index] = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
    text[32] = '\0';
    return std::string(text, 32);
}

// Chunk metadata lives in the frame header, so a producer writes the header
// object rather than a struct member.
void set_chunk(Frame& frame, const ChunkInfo& info) {
    JsonValue value = JsonValue::make_object();
    value.set("id", JsonValue(info.id));
    value.set("index", JsonValue(static_cast<std::int64_t>(info.index)));
    value.set("last", JsonValue(info.last));
    frame.header.set("chunk", std::move(value));
}

Result<Column> slice_column(const Column& source, const Type& type, std::int64_t start,
                            std::int64_t end) {
    Column out;
    out.length = end - start;
    if (!source.validity.empty()) {
        const auto length = static_cast<std::size_t>(out.length);
        std::vector<std::byte> validity((length + 7) / 8, std::byte{0});
        std::int64_t nulls = 0;
        for (std::int64_t row = 0; row < out.length; ++row) {
            if (!source.valid(start + row)) {
                ++nulls;
                continue;
            }
            const auto bit = static_cast<unsigned>(row) % 8;
            validity[static_cast<std::size_t>(row) / 8] |=
                static_cast<std::byte>(1u << bit);
        }
        out.null_count = nulls;
        out.validity = Buffer(std::move(validity));
    }
    if (type.kind == Type::Kind::Null) return out;

    if (type.kind == Type::Kind::Bool) {
        const auto length = static_cast<std::size_t>(out.length);
        std::vector<std::byte> data((length + 7) / 8, std::byte{0});
        const auto source_bytes = source.data.bytes();
        for (std::int64_t row = 0; row < out.length; ++row) {
            const auto bit = static_cast<std::size_t>(start + row);
            if (bit / 8 >= source_bytes.size()) break;
            const bool set =
                ((static_cast<unsigned>(source_bytes[bit / 8]) >> (bit % 8)) & 1u) != 0;
            if (!set) continue;
            data[static_cast<std::size_t>(row) / 8] |=
                static_cast<std::byte>(1u << (static_cast<unsigned>(row) % 8));
        }
        out.data = Buffer(std::move(data));
        return out;
    }

    const std::size_t width = type.width();
    if (width == 0) {
        const std::size_t offset_width = type.offset_width();
        const auto offsets = source.offsets.bytes();
        const auto entries = static_cast<std::size_t>(source.length) + 1;
        if (offset_width == 0 || offsets.size() < entries * offset_width) {
            return std::unexpected(chunk_error("column offsets are missing"));
        }
        auto read_offset = [&offsets, offset_width](std::size_t index) -> std::uint64_t {
            const auto* base = offsets.data() + index * offset_width;
            return offset_width == 4 ? detail::load_le<std::uint32_t>(base)
                                     : detail::load_le<std::uint64_t>(base);
        };
        const std::uint64_t first = read_offset(static_cast<std::size_t>(start));
        const std::uint64_t last = read_offset(static_cast<std::size_t>(end));
        if (last < first) {
            return std::unexpected(chunk_error("column offsets are not monotonic"));
        }
        std::vector<std::byte> rebased(
            (static_cast<std::size_t>(out.length) + 1) * offset_width);
        for (std::int64_t row = 0; row <= out.length; ++row) {
            const std::uint64_t value =
                read_offset(static_cast<std::size_t>(start + row)) - first;
            auto* target = rebased.data() + static_cast<std::size_t>(row) * offset_width;
            if (offset_width == 4) {
                detail::store_le<std::uint32_t>(target, static_cast<std::uint32_t>(value));
            } else {
                detail::store_le<std::uint64_t>(target, value);
            }
        }
        out.offsets = Buffer(std::move(rebased));
        auto data = buffer_slice(source.data.owner(), offset_in_owner(source.data) + first,
                                 static_cast<std::size_t>(last - first));
        if (!data) return std::unexpected(data.error());
        out.data = std::move(*data);
        return out;
    }

    const auto byte_start = static_cast<std::size_t>(start) * width;
    const auto byte_length = static_cast<std::size_t>(out.length) * width;
    if (byte_length == 0) return out;
    auto data = buffer_slice(source.data.owner(), offset_in_owner(source.data) + byte_start,
                             byte_length);
    if (!data) return std::unexpected(data.error());
    out.data = std::move(*data);
    return out;
}

}  // namespace

Result<void> validate_chunk_options(std::size_t rows_per_chunk, std::size_t max_payload) {
    if (rows_per_chunk == 0 || rows_per_chunk > kMaxChunkRows) {
        return std::unexpected(chunk_error("rows_per_chunk must be 1.." +
                                           std::to_string(kMaxChunkRows)));
    }
    if (max_payload == 0 || max_payload > kMaxPayloadBytes) {
        return std::unexpected(chunk_error("max_payload must be 1.." +
                                           std::to_string(kMaxPayloadBytes)));
    }
    return {};
}

Result<RecordBatch> slice_batch(const RecordBatch& batch, std::int64_t start,
                                std::int64_t end) {
    if (start < 0 || end < start || end > batch.rows()) {
        return std::unexpected(chunk_error("slice is outside the batch"));
    }
    if (batch.columns.size() != batch.schema.fields.size()) {
        return std::unexpected(chunk_error("batch columns do not match its schema"));
    }
    RecordBatch out;
    out.schema = batch.schema;
    out.columns.reserve(batch.columns.size());
    for (std::size_t index = 0; index < batch.columns.size(); ++index) {
        auto column = slice_column(batch.columns[index], batch.schema.fields[index].type,
                                   start, end);
        if (!column) return std::unexpected(column.error());
        out.columns.push_back(std::move(*column));
    }
    return out;
}

ChunkProducer::ChunkProducer(RecordBatch batch, LaneId lane, std::size_t rows_per_chunk,
                             std::size_t max_payload, LaneOptions options)
    : batch_(std::move(batch)),
      lane_(lane),
      rows_per_chunk_(rows_per_chunk),
      max_payload_(max_payload),
      options_(std::move(options)),
      stream_id_(make_stream_id()) {
    if (batch_.rows() == 0) {
        ranges_.emplace_back(0, 0);
        return;
    }
    for (std::int64_t start = 0; start < batch_.rows();
         start += static_cast<std::int64_t>(rows_per_chunk_)) {
        const std::int64_t end =
            std::min<std::int64_t>(batch_.rows(),
                                   start + static_cast<std::int64_t>(rows_per_chunk_));
        ranges_.emplace_back(start, end);
    }
}

bool ChunkProducer::next(std::vector<std::byte>& out) {
    while (!ranges_.empty()) {
        const auto [start, end] = ranges_.front();
        ranges_.pop_front();
        auto slice = slice_batch(batch_, start, end);
        if (!slice) {
            failed_ = true;
            failure_code_ = slice.error().code;
            failure_message_ = slice.error().message;
            return false;
        }
        auto frame = encode_lane(lane_, *slice, options_);
        if (!frame) {
            failed_ = true;
            failure_code_ = frame.error().code;
            failure_message_ = frame.error().message;
            return false;
        }
        if (frame->payload.size() > max_payload_) {
            if (end - start <= 1) {
                failed_ = true;
                failure_code_ = Code::OversizeFrame;
                failure_message_ = "one row encodes to " +
                                   std::to_string(frame->payload.size()) + " bytes, over " +
                                   std::to_string(max_payload_);
                return false;
            }
            const std::int64_t middle = start + (end - start) / 2;
            ranges_.emplace_front(middle, end);
            ranges_.emplace_front(start, middle);
            continue;
        }
        if (index_ >= kMaxChunks) {
            failed_ = true;
            failure_code_ = Code::Chunk;
            failure_message_ = "stream exceeds " + std::to_string(kMaxChunks) + " chunks";
            return false;
        }
        set_chunk(*frame, ChunkInfo{stream_id_, index_, ranges_.empty()});
        auto wire = encode_frame(*frame, max_payload_);
        if (!wire) {
            failed_ = true;
            failure_code_ = wire.error().code;
            failure_message_ = wire.error().message;
            return false;
        }
        out = std::move(*wire);
        ++index_;
        ++encoded_;
        return true;
    }
    return false;
}

ChunkAssembler::ChunkAssembler(std::size_t max_payload, std::size_t max_uncompressed)
    : max_payload_(max_payload), max_uncompressed_(max_uncompressed) {}

Result<RecordBatch> ChunkAssembler::add(std::span<const std::byte> frame_bytes) {
    auto frame = decode_frame(frame_bytes, max_payload_, max_uncompressed_);
    if (!frame) return std::unexpected(frame.error());
    const auto info = frame->chunk();
    if (!info) return std::unexpected(chunk_error("frame carries no chunk metadata"));
    if (!stream_id_.empty() && info->id != stream_id_) {
        return std::unexpected(chunk_error("chunk id changed mid stream"));
    }
    if (saw_last_) {
        return std::unexpected(chunk_error("chunk after the last chunk"));
    }
    if (info->index != next_index_) {
        return std::unexpected(
            chunk_error("chunk index " + std::to_string(info->index) + ", expected " +
                        std::to_string(next_index_)));
    }
    auto batch = decode_lane(*frame, LaneOptions{}, max_uncompressed_);
    if (!batch) return std::unexpected(batch.error());
    auto encoded = encode_schema_message(batch->schema);
    if (!encoded) return std::unexpected(encoded.error());
    std::string key(reinterpret_cast<const char*>(encoded->data()), encoded->size());
    if (!schema_key_) {
        schema_key_ = key;
    } else if (*schema_key_ != key) {
        return std::unexpected(chunk_error("chunk schema changed mid stream"));
    }
    stream_id_ = info->id;
    if (info->last) saw_last_ = true;
    ++next_index_;
    return batch;
}

Result<void> ChunkAssembler::check_complete() const {
    if (next_index_ == 0) return std::unexpected(chunk_error("chunk stream is empty"));
    if (!saw_last_) {
        return std::unexpected(chunk_error("stream ended before the last chunk"));
    }
    return {};
}

}  // namespace arrowlane
