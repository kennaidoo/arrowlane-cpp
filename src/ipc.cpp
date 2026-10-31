#include "arrowlane/ipc.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

#include "arrowlane/schema.hpp"
#include "detail/bytes.hpp"
#include "detail/flatbuf.hpp"
#include "detail/schema_tags.hpp"

namespace arrowlane {
namespace {

constexpr std::uint64_t align8(std::uint64_t value) noexcept {
    return (value + 7) & ~std::uint64_t{7};
}

std::size_t expected_buffers(const Schema& schema) {
    std::size_t count = 0;
    for (const Field& field : schema.fields) {
        if (field.type.kind == Type::Kind::Null) continue;
        count += field.type.is_variable_length() ? 3 : 2;
    }
    return count;
}

Result<void> place(std::span<std::byte> body, const IpcBuffer& target,
                   std::span<const std::byte> source) {
    if (source.size() != target.length ||
        target.offset + target.length > body.size()) {
        return std::unexpected(make_error(Code::Invalid,
                                          "ipc buffer does not match the layout"));
    }
    if (!source.empty()) {
        std::memcpy(body.data() + target.offset, source.data(), source.size());
    }
    return {};
}

const std::array<std::byte, 8> kEndOfStream{std::byte{0xFF}, std::byte{0xFF},
                                            std::byte{0xFF}, std::byte{0xFF},
                                            std::byte{0},    std::byte{0},
                                            std::byte{0},    std::byte{0}};

}  // namespace

Result<IpcLayout> plan_ipc_body(const RecordBatch& batch) {
    auto valid = validate_batch(batch);
    if (!valid) return std::unexpected(valid.error());
    IpcLayout layout;
    layout.nodes.reserve(batch.columns.size());
    std::uint64_t offset = 0;
    for (std::size_t index = 0; index < batch.columns.size(); ++index) {
        const Column& column = batch.columns[index];
        const Type& type = batch.schema.fields[index].type;
        layout.nodes.emplace_back(column.length, column.null_count);
        if (type.kind == Type::Kind::Null) continue;
        layout.buffers.push_back(IpcBuffer{offset, column.validity.size()});
        offset = align8(offset + column.validity.size());
        if (type.is_variable_length()) {
            layout.buffers.push_back(IpcBuffer{offset, column.offsets.size()});
            offset = align8(offset + column.offsets.size());
        }
        layout.buffers.push_back(IpcBuffer{offset, column.data.size()});
        offset = align8(offset + column.data.size());
    }
    layout.body_size = align8(offset);
    return layout;
}

// Writes every column into the caller buffer at its planned offset. The
// caller owns the buffer, so the body is written once, in place.
Result<void> place_ipc_body(const RecordBatch& batch, const IpcLayout& layout,
                            std::span<std::byte> body) {
    if (layout.buffers.size() != expected_buffers(batch.schema)) {
        return std::unexpected(make_error(Code::Invalid,
                                          "ipc layout has the wrong buffer count"));
    }
    if (body.size() != static_cast<std::size_t>(layout.body_size)) {
        return std::unexpected(make_error(Code::Invalid, "ipc body size mismatch"));
    }
    std::size_t buffer = 0;
    for (std::size_t index = 0; index < batch.columns.size(); ++index) {
        const Column& column = batch.columns[index];
        const Type& type = batch.schema.fields[index].type;
        if (type.kind == Type::Kind::Null) continue;
        auto validity = place(body, layout.buffers[buffer++], column.validity.bytes());
        if (!validity) return std::unexpected(validity.error());
        if (type.is_variable_length()) {
            auto offsets = place(body, layout.buffers[buffer++], column.offsets.bytes());
            if (!offsets) return std::unexpected(offsets.error());
        }
        auto data = place(body, layout.buffers[buffer++], column.data.bytes());
        if (!data) return std::unexpected(data.error());
    }
    return {};
}

// The public helper keeps its owning shape for callers and tests. The encoder
// does not use it: it calls place_ipc_body directly into the wire buffer.
Result<std::vector<std::byte>> build_ipc_body(const RecordBatch& batch,
                                              const IpcLayout& layout) {
    std::vector<std::byte> body(static_cast<std::size_t>(layout.body_size), std::byte{0});
    auto placed = place_ipc_body(batch, layout, body);
    if (!placed) return std::unexpected(placed.error());
    return body;
}

Result<std::vector<std::byte>> encode_ipc(const RecordBatch& batch,
                                          std::size_t headroom) {
    auto schema_message = encode_schema_message(batch.schema);
    if (!schema_message) return std::unexpected(schema_message.error());
    auto layout = plan_ipc_body(batch);
    if (!layout) return std::unexpected(layout.error());

    // The writer holds the two flatbuffer messages, a few hundred bytes each.
    detail::FlatWriter writer(4096);
    auto message = writer.begin_table(4);
    writer.add_i16(message, 0, detail::kMessageVersionV5);
    writer.add_u8(message, 1, detail::kMessageHeaderRecordBatch);
    const std::size_t header_field = writer.add_offset(message, 2);
    writer.add_i64(message, 3, layout->body_size);
    writer.end_table(message);

    auto record = writer.begin_table(3);
    writer.add_i64(record, 0, batch.rows());
    const std::size_t nodes_field = writer.add_offset(record, 1);
    const std::size_t buffers_field = writer.add_offset(record, 2);
    writer.end_table(record);

    auto nodes = writer.add_struct_vector(layout->nodes.size(), 16, 8);
    for (std::size_t index = 0; index < layout->nodes.size(); ++index) {
        std::array<std::byte, 16> raw{};
        detail::store_le<std::int64_t>(raw.data(), layout->nodes[index].first);
        detail::store_le<std::int64_t>(raw.data() + 8, layout->nodes[index].second);
        writer.write_struct(nodes, index, raw);
    }
    writer.patch_offset(nodes_field, nodes.pos);

    auto buffers = writer.add_struct_vector(layout->buffers.size(), 16, 8);
    for (std::size_t index = 0; index < layout->buffers.size(); ++index) {
        std::array<std::byte, 16> raw{};
        detail::store_le<std::int64_t>(raw.data(),
                                       static_cast<std::int64_t>(layout->buffers[index].offset));
        detail::store_le<std::int64_t>(raw.data() + 8,
                                       static_cast<std::int64_t>(layout->buffers[index].length));
        writer.write_struct(buffers, index, raw);
    }
    writer.patch_offset(buffers_field, buffers.pos);
    writer.patch_offset(header_field, record.pos);

    // One allocation for the whole frame. The body is placed into the region
    // it will occupy in the wire, so nothing is copied twice.
    std::vector<std::byte> out;
    if (headroom == 0) {
        out = std::move(*schema_message);
    } else {
        out.assign(headroom, std::byte{0});
        out.insert(out.end(), schema_message->begin(), schema_message->end());
    }
    auto meta = detail::encapsulate(writer.finish(message.pos));
    const auto body_size = static_cast<std::size_t>(layout->body_size);
    const std::size_t body_start = out.size() + meta.size();
    out.reserve(body_start + body_size + kEndOfStream.size());
    out.insert(out.end(), meta.begin(), meta.end());
    out.resize(body_start + body_size);
    auto placed = place_ipc_body(batch, *layout,
                                 std::span<std::byte>(out.data() + body_start, body_size));
    if (!placed) return std::unexpected(placed.error());
    out.insert(out.end(), kEndOfStream.begin(), kEndOfStream.end());
    return out;
}

// ---------------------------------------------------------------- decoding

namespace {

struct MessageHeader {
    std::span<const std::byte> metadata;
    std::size_t body_start = 0;
    std::uint32_t length = 0;
};

Result<MessageHeader> read_message_header(std::span<const std::byte> stream,
                                          std::size_t pos) {
    if (pos + 4 > stream.size()) {
        return std::unexpected(
            make_error(Code::TruncatedFrame, "stream ends inside a message length"));
    }
    const std::uint32_t first = detail::load_le<std::uint32_t>(stream.data() + pos);
    std::size_t metadata_start = 0;
    std::uint32_t length = 0;
    if (first == 0xFFFFFFFFU) {
        if (pos + 8 > stream.size()) {
            return std::unexpected(
                make_error(Code::TruncatedFrame, "stream ends inside a message prefix"));
        }
        length = detail::load_le<std::uint32_t>(stream.data() + pos + 4);
        metadata_start = pos + 8;
    } else {
        length = first;
        metadata_start = pos + 4;
    }
    if (length == 0) {
        return MessageHeader{std::span<const std::byte>{}, metadata_start, 0};
    }
    if (metadata_start + static_cast<std::size_t>(length) > stream.size()) {
        return std::unexpected(make_error(
            Code::TruncatedFrame, "message metadata exceeds the stream"));
    }
    return MessageHeader{stream.subspan(metadata_start, length),
                         metadata_start + static_cast<std::size_t>(length), length};
}

Result<Buffer> take_buffer(const detail::FlatReader& reader,
                           const detail::FlatReader::Vector& buffers, std::size_t index,
                           std::span<const std::byte> payload,
                           const std::shared_ptr<const std::vector<std::byte>>& owner,
                           std::size_t base, std::uint64_t needed) {
    const auto raw = reader.struct_at(buffers, index, 16);
    if (!raw) {
        return std::unexpected(
            make_error(Code::BadHeader, "buffer descriptor is out of bounds"));
    }
    const std::int64_t offset = detail::load_le<std::int64_t>(raw->data());
    const std::int64_t length = detail::load_le<std::int64_t>(raw->data() + 8);
    if (offset < 0 || length < 0) {
        return std::unexpected(
            make_error(Code::BadHeader, "buffer descriptor is negative"));
    }
    const auto start = static_cast<std::uint64_t>(offset);
    const auto size = static_cast<std::uint64_t>(length);
    if (start > payload.size() || size > payload.size() - start) {
        return std::unexpected(make_error(Code::BadHeader, "buffer exceeds the body"));
    }
    // Arrow permits a buffer longer than the node needs, for example when the
    // producer wrote a slice of a larger array. Slice to the logical length.
    if (size < needed) {
        return std::unexpected(make_error(
            Code::BadHeader, "buffer is shorter than the node requires"));
    }
    if (base > owner->size() || start + needed > owner->size() - base) {
        return std::unexpected(make_error(Code::Invalid, "buffer exceeds the owner"));
    }
    return buffer_slice(owner, base + static_cast<std::size_t>(start),
                        static_cast<std::size_t>(needed));
}

bool bit_at(const Buffer& buffer, std::int64_t row) {
    const auto index = static_cast<std::size_t>(row);
    return (buffer[index / 8] & (std::byte{1} << (index % 8))) != std::byte{0};
}

void set_bit(std::vector<std::byte>& bytes, std::int64_t row) {
    const auto index = static_cast<std::size_t>(row);
    bytes[index / 8] |= std::byte{1} << (index % 8);
}

void push_offset(std::vector<std::byte>& out, const Type& type, std::uint64_t value) {
    if (type.has_offsets_64()) {
        std::array<std::byte, 8> raw{};
        detail::store_le<std::uint64_t>(raw.data(), value);
        out.insert(out.end(), raw.begin(), raw.end());
    } else {
        std::array<std::byte, 4> raw{};
        detail::store_le<std::uint32_t>(raw.data(), static_cast<std::uint32_t>(value));
        out.insert(out.end(), raw.begin(), raw.end());
    }
}

Column merge_columns(const Type& type, const std::vector<Column>& parts) {
    Column merged;
    std::int64_t rows = 0;
    std::int64_t nulls = 0;
    for (const Column& part : parts) {
        rows += part.length;
        nulls += part.null_count;
    }
    merged.length = rows;
    merged.null_count = nulls;
    if (type.kind == Type::Kind::Null) return merged;
    if (nulls > 0) {
        std::vector<std::byte> validity(
            static_cast<std::size_t>((rows + 7) / 8), std::byte{0});
        std::int64_t row = 0;
        for (const Column& part : parts) {
            for (std::int64_t index = 0; index < part.length; ++index) {
                if (part.valid(index)) set_bit(validity, row + index);
            }
            row += part.length;
        }
        merged.validity = Buffer(std::move(validity));
    }
    if (type.is_variable_length()) {
        std::vector<std::byte> offsets;
        offsets.reserve(static_cast<std::size_t>((rows + 1) * type.offset_width()));
        std::vector<std::byte> data;
        std::uint64_t running = 0;
        push_offset(offsets, type, 0);
        for (const Column& part : parts) {
            for (std::int64_t index = 0; index < part.length; ++index) {
                const auto value = part.bytes_at(index);
                running += value.size();
                push_offset(offsets, type, running);
                data.insert(data.end(), value.begin(), value.end());
            }
        }
        merged.offsets = Buffer(std::move(offsets));
        merged.data = Buffer(std::move(data));
        return merged;
    }
    if (type.kind == Type::Kind::Bool) {
        std::vector<std::byte> data(static_cast<std::size_t>((rows + 7) / 8),
                                    std::byte{0});
        std::int64_t row = 0;
        for (const Column& part : parts) {
            for (std::int64_t index = 0; index < part.length; ++index) {
                if (bit_at(part.data, index)) set_bit(data, row + index);
            }
            row += part.length;
        }
        merged.data = Buffer(std::move(data));
        return merged;
    }
    std::vector<std::byte> data;
    data.reserve(static_cast<std::size_t>(rows) * type.width());
    for (const Column& part : parts) {
        data.insert(data.end(), part.data.bytes().begin(), part.data.bytes().end());
    }
    merged.data = Buffer(std::move(data));
    return merged;
}

Result<void> validate_column(const Column& column, const Type& type,
                             const std::string& name) {
    if (column.length < 0 || column.null_count < 0 ||
        column.null_count > column.length) {
        return std::unexpected(make_error(
            Code::BadHeader, "column " + name + " carries an invalid node"));
    }
    if (column.null_count > 0) {
        const auto expected = static_cast<std::size_t>((column.length + 7) / 8);
        if (column.validity.size() < expected) {
            return std::unexpected(make_error(
                Code::BadHeader, "column " + name + " validity is too short"));
        }
    } else if (!column.validity.empty() && column.validity.size() != 0) {
        // Arrow permits a validity buffer with no nulls; the bits must be set.
        for (std::int64_t row = 0; row < column.length; ++row) {
            if (!column.valid(row)) {
                return std::unexpected(make_error(
                    Code::BadHeader, "column " + name + " has a clear validity bit"));
            }
        }
    }
    if (type.kind == Type::Kind::Null) {
        if (!column.data.empty()) {
            return std::unexpected(make_error(
                Code::BadHeader, "null column " + name + " carries data"));
        }
        return {};
    }
    if (type.is_variable_length()) {
        const std::size_t width = type.offset_width();
        const auto entries = static_cast<std::size_t>(column.length) + 1;
        if (column.offsets.size() != entries * width) {
            return std::unexpected(make_error(
                Code::BadHeader, "column " + name + " has the wrong offset count"));
        }
        std::uint64_t previous = 0;
        for (std::size_t entry = 0; entry < entries; ++entry) {
            const std::uint64_t current =
                width == 4 ? detail::load_le<std::uint32_t>(column.offsets.bytes().data() +
                                                            entry * 4)
                           : detail::load_le<std::uint64_t>(
                                 column.offsets.bytes().data() + entry * 8);
            if (current < previous || current > column.data.size()) {
                return std::unexpected(make_error(
                    Code::BadHeader, "column " + name + " offsets are not monotonic"));
            }
            previous = current;
        }
        if (previous != column.data.size()) {
            return std::unexpected(make_error(
                Code::BadHeader, "column " + name + " last offset does not match data"));
        }
        return {};
    }
    if (type.kind == Type::Kind::Bool) {
        const auto expected = static_cast<std::size_t>((column.length + 7) / 8);
        if (column.data.size() != expected) {
            return std::unexpected(make_error(
                Code::BadHeader, "column " + name + " has the wrong bit count"));
        }
        return {};
    }
    const auto expected =
        static_cast<std::size_t>(column.length) * type.width();
    if (column.data.size() != expected) {
        return std::unexpected(make_error(
            Code::BadHeader,
            "column " + name + " has " + std::to_string(column.data.size()) +
                " value bytes, expected " + std::to_string(expected) + " for " +
                std::to_string(column.length) + " rows"));
    }
    return {};
}

Result<RecordBatch> decode_record_batch(
    const Schema& schema, const detail::FlatReader& reader,
    const detail::FlatReader::Table& table, std::span<const std::byte> payload,
    const std::shared_ptr<const std::vector<std::byte>>& owner, std::size_t base) {
    if (reader.field(table, 3)) {
        return std::unexpected(
            make_error(Code::Unsupported, "body compression is not supported"));
    }
    const auto length = reader.i64(table, 0);
    if (!length || *length < 0) {
        return std::unexpected(
            make_error(Code::BadHeader, "record batch length is invalid"));
    }
    const auto nodes = reader.vector(table, 1);
    const auto buffers = reader.vector(table, 2);
    if (!nodes || !buffers) {
        return std::unexpected(
            make_error(Code::BadHeader, "record batch carries no nodes or buffers"));
    }
    if (nodes->count != schema.fields.size()) {
        return std::unexpected(make_error(
            Code::BadHeader, "node count does not match the schema"));
    }
    if (buffers->count != expected_buffers(schema)) {
        return std::unexpected(make_error(
            Code::BadHeader, "buffer count does not match the schema"));
    }
    RecordBatch batch;
    batch.schema = schema;
    std::size_t buffer_index = 0;
    for (std::size_t index = 0; index < schema.fields.size(); ++index) {
        const Type& type = schema.fields[index].type;
        const auto node = reader.struct_at(*nodes, index, 16);
        if (!node) {
            return std::unexpected(
                make_error(Code::BadHeader, "node is out of bounds"));
        }
        Column column;
        column.length = detail::load_le<std::int64_t>(node->data());
        column.null_count = detail::load_le<std::int64_t>(node->data() + 8);
        if (column.length != *length) {
            return std::unexpected(make_error(
                Code::BadHeader, "node length does not match the batch length"));
        }
        if (column.length > static_cast<std::int64_t>(kMaxChunkRows)) {
            return std::unexpected(make_error(
                Code::BadHeader, "node length exceeds the row cap"));
        }
        if (type.kind != Type::Kind::Null) {
            const std::uint64_t validity_needed =
                column.null_count > 0
                    ? static_cast<std::uint64_t>((column.length + 7) / 8)
                    : 0;
            auto validity = take_buffer(reader, *buffers, buffer_index++, payload, owner,
                                        base, validity_needed);
            if (!validity) return std::unexpected(validity.error());
            if (column.null_count > 0) {
                column.validity = std::move(*validity);
            }
            std::uint64_t data_needed = 0;
            if (type.is_variable_length()) {
                const auto offsets_needed =
                    static_cast<std::uint64_t>(column.length + 1) * type.offset_width();
                auto offsets = take_buffer(reader, *buffers, buffer_index++, payload,
                                           owner, base, offsets_needed);
                if (!offsets) return std::unexpected(offsets.error());
                column.offsets = std::move(*offsets);
                const auto width = static_cast<std::size_t>(type.offset_width());
                const auto entry =
                    static_cast<std::size_t>(column.length) * width;
                data_needed =
                    width == 4
                        ? detail::load_le<std::uint32_t>(column.offsets.bytes().data() +
                                                         entry)
                        : detail::load_le<std::uint64_t>(column.offsets.bytes().data() +
                                                         entry);
            } else if (type.kind == Type::Kind::Bool) {
                data_needed = static_cast<std::uint64_t>((column.length + 7) / 8);
            } else {
                data_needed =
                    static_cast<std::uint64_t>(column.length) * type.width();
            }
            auto data = take_buffer(reader, *buffers, buffer_index++, payload, owner,
                                    base, data_needed);
            if (!data) return std::unexpected(data.error());
            column.data = std::move(*data);
        }
        auto valid = validate_column(column, type, schema.fields[index].name);
        if (!valid) return std::unexpected(valid.error());
        batch.columns.push_back(std::move(column));
    }
    return batch;
}

}  // namespace

Result<RecordBatch> decode_ipc_view(
    std::span<const std::byte> stream, std::shared_ptr<const std::vector<std::byte>> owner,
    std::size_t base, std::size_t max_uncompressed) {
    if (!owner) {
        return std::unexpected(make_error(Code::Invalid, "buffer owner is null"));
    }
    if (base > owner->size() || stream.size() > owner->size() - base) {
        return std::unexpected(make_error(Code::Invalid, "stream exceeds the owner"));
    }
    if (owner->data() + base != stream.data()) {
        return std::unexpected(
            make_error(Code::Invalid, "stream does not start at the owner offset"));
    }
    if (stream.size() > max_uncompressed) {
        return std::unexpected(make_error(
            Code::OversizeFrame, "stream " + std::to_string(stream.size()) + " exceeds " +
                                     std::to_string(max_uncompressed)));
    }

    Schema schema;
    bool have_schema = false;
    std::vector<RecordBatch> batches;
    std::size_t pos = 0;
    std::size_t messages = 0;
    while (pos < stream.size()) {
        if (++messages > 1'000'000) {
            return std::unexpected(
                make_error(Code::Limit, "stream carries more than 1000000 messages"));
        }
        auto header = read_message_header(stream, pos);
        if (!header) return std::unexpected(header.error());
        if (header->length == 0) break;
        const detail::FlatReader reader(header->metadata);
        const auto message = reader.root();
        if (!message) {
            return std::unexpected(
                make_error(Code::BadHeader, "message is not a flatbuffer"));
        }
        const auto version = reader.i16(*message, 0);
        if (!version || *version != detail::kMessageVersionV5) {
            return std::unexpected(
                make_error(Code::BadHeader, "message version must be V5"));
        }
        const auto tag = reader.u8(*message, 1);
        if (!tag) {
            return std::unexpected(
                make_error(Code::BadHeader, "message carries no header type"));
        }
        const std::int64_t body_length = reader.i64(*message, 3).value_or(0);
        if (body_length < 0) {
            return std::unexpected(
                make_error(Code::BadHeader, "message body length is negative"));
        }
        if (header->body_start + static_cast<std::size_t>(body_length) > stream.size()) {
            return std::unexpected(
                make_error(Code::TruncatedFrame, "message body exceeds the stream"));
        }
        const auto payload = stream.subspan(header->body_start,
                                            static_cast<std::size_t>(body_length));
        const auto header_pos = reader.target(*message, 2);
        if (!header_pos) {
            return std::unexpected(
                make_error(Code::BadHeader, "message carries no header"));
        }
        const auto table = reader.table_at(*header_pos);
        if (!table) {
            return std::unexpected(
                make_error(Code::BadHeader, "message header is not a table"));
        }
        if (*tag == 2) {
            return std::unexpected(make_error(
                Code::Unsupported, "dictionary batches are not supported"));
        }
        if (*tag == detail::kMessageHeaderSchema) {
            std::vector<std::byte> encapsulated(8 + header->length, std::byte{0});
            detail::store_le<std::uint32_t>(encapsulated.data(), 0xFFFFFFFFU);
            detail::store_le<std::uint32_t>(encapsulated.data() + 4, header->length);
            std::memcpy(encapsulated.data() + 8, header->metadata.data(),
                        header->length);
            auto decoded = decode_schema_message(encapsulated);
            if (!decoded) return std::unexpected(decoded.error());
            if (have_schema && !(*decoded == schema)) {
                return std::unexpected(make_error(
                    Code::BadHeader, "stream carries a second, different schema"));
            }
            schema = std::move(*decoded);
            have_schema = true;
        } else if (*tag == detail::kMessageHeaderRecordBatch) {
            if (!have_schema) {
                return std::unexpected(make_error(
                    Code::BadHeader, "record batch before the schema"));
            }
            auto batch = decode_record_batch(schema, reader, *table, payload, owner,
                                             base + header->body_start);
            if (!batch) return std::unexpected(batch.error());
            batches.push_back(std::move(*batch));
        } else {
            return std::unexpected(make_error(
                Code::Unsupported, "message header type is not supported"));
        }
        pos = header->body_start + static_cast<std::size_t>(body_length);
    }
    if (!have_schema) {
        return std::unexpected(
            make_error(Code::BadHeader, "stream carries no schema"));
    }
    if (batches.empty()) {
        // A stream with no record batch still describes its columns. Build one
        // empty column per field so the batch is self consistent.
        RecordBatch empty;
        empty.schema = std::move(schema);
        for (const Field& field : empty.schema.fields) {
            ColumnBuilder builder(field.type);
            empty.columns.push_back(builder.finish());
        }
        return empty;
    }
    if (batches.size() == 1) return std::move(batches.front());

    RecordBatch merged;
    merged.schema = std::move(schema);
    for (std::size_t index = 0; index < merged.schema.fields.size(); ++index) {
        std::vector<Column> parts;
        parts.reserve(batches.size());
        for (const RecordBatch& batch : batches) {
            parts.push_back(batch.columns[index]);
        }
        merged.columns.push_back(merge_columns(merged.schema.fields[index].type, parts));
    }
    return merged;
}

Result<RecordBatch> decode_ipc(std::span<const std::byte> stream,
                               std::size_t max_uncompressed) {
    auto owner = std::make_shared<const std::vector<std::byte>>(stream.begin(),
                                                                stream.end());
    return decode_ipc_view(std::span<const std::byte>(owner->data(), owner->size()), owner,
                           0, max_uncompressed);
}

}  // namespace arrowlane
