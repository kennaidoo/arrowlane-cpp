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
        // A dictionary column holds int32 indices, so it has no offsets buffer
        // even when the value type is variable length.
        count += (field.dictionary || !field.type.is_variable_length()) ? 2 : 3;
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
        const Field& field = batch.schema.fields[index];
        const Type& type = field.type;
        layout.nodes.emplace_back(column.length, column.null_count);
        if (type.kind == Type::Kind::Null) continue;
        layout.buffers.push_back(IpcBuffer{offset, column.validity.size()});
        offset = align8(offset + column.validity.size());
        if (!field.dictionary && type.is_variable_length()) {
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
        const Field& field = batch.schema.fields[index];
        const Type& type = field.type;
        if (type.kind == Type::Kind::Null) continue;
        auto validity = place(body, layout.buffers[buffer++], column.validity.bytes());
        if (!validity) return std::unexpected(validity.error());
        if (!field.dictionary && type.is_variable_length()) {
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

// Writes the RecordBatch table with its node and buffer vectors and returns the
// table position. Shared by the record batch and by every dictionary batch, so
// both carry metadata of identical shape.
std::size_t write_record_batch_metadata(detail::FlatWriter& writer,
                                        const RecordBatch& batch,
                                        const IpcLayout& layout) {
    auto record = writer.begin_table(3);
    writer.add_i64(record, 0, batch.rows());
    const std::size_t nodes_field = writer.add_offset(record, 1);
    const std::size_t buffers_field = writer.add_offset(record, 2);
    writer.end_table(record);

    auto nodes = writer.add_struct_vector(layout.nodes.size(), 16, 8);
    for (std::size_t index = 0; index < layout.nodes.size(); ++index) {
        std::array<std::byte, 16> raw{};
        detail::store_le<std::int64_t>(raw.data(), layout.nodes[index].first);
        detail::store_le<std::int64_t>(raw.data() + 8, layout.nodes[index].second);
        writer.write_struct(nodes, index, raw);
    }
    writer.patch_offset(nodes_field, nodes.pos);

    auto buffers = writer.add_struct_vector(layout.buffers.size(), 16, 8);
    for (std::size_t index = 0; index < layout.buffers.size(); ++index) {
        std::array<std::byte, 16> raw{};
        detail::store_le<std::int64_t>(
            raw.data(), static_cast<std::int64_t>(layout.buffers[index].offset));
        detail::store_le<std::int64_t>(
            raw.data() + 8, static_cast<std::int64_t>(layout.buffers[index].length));
        writer.write_struct(buffers, index, raw);
    }
    writer.patch_offset(buffers_field, buffers.pos);
    return record.pos;
}

namespace {

// A dictionary batch that is already built: its metadata and the body it describes.
struct IpcDictionary {
    std::vector<std::byte> meta;
    std::vector<std::byte> body;
};

// The whole stream, built before any output buffer is touched. Knowing the total
// size up front is what lets a caller hand in a buffer and have it checked before
// a single byte is written.
struct IpcStream {
    std::vector<std::byte> schema_message;
    std::vector<IpcDictionary> dictionaries;
    std::vector<std::byte> record_meta;
    std::size_t record_body_size = 0;

    [[nodiscard]] std::size_t total(std::size_t headroom) const noexcept {
        std::size_t size = headroom + schema_message.size();
        for (const IpcDictionary& dictionary : dictionaries) {
            size += dictionary.meta.size() + dictionary.body.size();
        }
        return size + record_meta.size() + record_body_size + kEndOfStream.size();
    }
};

Result<IpcStream> build_ipc_stream(const RecordBatch& batch, const IpcLayout& layout) {
    IpcStream stream;
    auto schema_message = encode_schema_message(batch.schema);
    if (!schema_message) return std::unexpected(schema_message.error());
    stream.schema_message = std::move(*schema_message);

    // The schema message first, then one dictionary batch per dictionary, then the
    // record batch. Arrow requires each dictionary before the batch that indexes it.
    for (const DictionaryColumn& dictionary : batch.dictionaries) {
        // A dictionary batch is a record batch with one column: the values.
        RecordBatch values;
        values.schema.fields.push_back(
            Field{"dictionary_values", dictionary.value_type, false, std::nullopt});
        values.columns.push_back(dictionary.values);
        auto values_layout = plan_ipc_body(values);
        if (!values_layout) return std::unexpected(values_layout.error());
        auto values_body = build_ipc_body(values, *values_layout);
        if (!values_body) return std::unexpected(values_body.error());

        detail::FlatWriter writer(4096);
        auto message = writer.begin_table(4);
        writer.add_i16(message, 0, detail::kMessageVersionV5);
        writer.add_u8(message, 1, detail::kMessageHeaderDictionaryBatch);
        const std::size_t header_field = writer.add_offset(message, 2);
        writer.add_i64(message, 3, values_layout->body_size);
        writer.end_table(message);

        auto batch_table = writer.begin_table(3);
        writer.add_i64(batch_table, 0, dictionary.id);
        const std::size_t data_field = writer.add_offset(batch_table, 1);
        writer.add_bool(batch_table, 2, false);  // isDelta, never true here
        writer.end_table(batch_table);

        const std::size_t data_pos =
            write_record_batch_metadata(writer, values, *values_layout);
        writer.patch_offset(data_field, data_pos);
        writer.patch_offset(header_field, batch_table.pos);

        stream.dictionaries.push_back(
            IpcDictionary{detail::encapsulate(writer.finish(message.pos)),
                          std::move(*values_body)});
    }

    detail::FlatWriter writer(4096);
    auto message = writer.begin_table(4);
    writer.add_i16(message, 0, detail::kMessageVersionV5);
    writer.add_u8(message, 1, detail::kMessageHeaderRecordBatch);
    const std::size_t header_field = writer.add_offset(message, 2);
    writer.add_i64(message, 3, layout.body_size);
    writer.end_table(message);

    const std::size_t record_pos = write_record_batch_metadata(writer, batch, layout);
    writer.patch_offset(header_field, record_pos);
    stream.record_meta = detail::encapsulate(writer.finish(message.pos));
    stream.record_body_size = static_cast<std::size_t>(layout.body_size);
    return stream;
}

// Copies the built pieces into `out` in wire order. The record body is placed
// directly into the region it will occupy, so nothing is copied twice.
Result<std::span<std::byte>> write_ipc_stream(const RecordBatch& batch,
                                             const IpcStream& stream,
                                             const IpcLayout& layout,
                                             std::size_t headroom,
                                             std::span<std::byte> out) {
    const std::size_t total = stream.total(headroom);
    if (out.size() < total) {
        return std::unexpected(make_error(
            Code::Limit, "the caller buffer is smaller than the encoded stream"));
    }
    std::byte* cursor = out.data();
    std::memset(cursor, 0, headroom);
    cursor += headroom;
    std::memcpy(cursor, stream.schema_message.data(), stream.schema_message.size());
    cursor += stream.schema_message.size();
    for (const IpcDictionary& dictionary : stream.dictionaries) {
        std::memcpy(cursor, dictionary.meta.data(), dictionary.meta.size());
        cursor += dictionary.meta.size();
        std::memcpy(cursor, dictionary.body.data(), dictionary.body.size());
        cursor += dictionary.body.size();
    }
    std::memcpy(cursor, stream.record_meta.data(), stream.record_meta.size());
    cursor += stream.record_meta.size();
    auto placed = place_ipc_body(
        batch, layout, std::span<std::byte>(cursor, stream.record_body_size));
    if (!placed) return std::unexpected(placed.error());
    cursor += stream.record_body_size;
    std::memcpy(cursor, kEndOfStream.data(), kEndOfStream.size());
    return out.first(total);
}

}  // namespace

Result<std::vector<std::byte>> encode_ipc(const RecordBatch& batch, std::size_t headroom) {
    auto layout = plan_ipc_body(batch);
    if (!layout) return std::unexpected(layout.error());
    auto stream = build_ipc_stream(batch, *layout);
    if (!stream) return std::unexpected(stream.error());

    // One allocation for the whole frame, sized exactly.
    std::vector<std::byte> out(stream->total(headroom));
    auto written =
        write_ipc_stream(batch, *stream, *layout, headroom, std::span<std::byte>(out));
    if (!written) return std::unexpected(written.error());
    return out;
}

Result<std::span<std::byte>> encode_ipc_into(const RecordBatch& batch,
                                            std::span<std::byte> buffer) {
    auto layout = plan_ipc_body(batch);
    if (!layout) return std::unexpected(layout.error());
    auto stream = build_ipc_stream(batch, *layout);
    if (!stream) return std::unexpected(stream.error());
    return write_ipc_stream(batch, *stream, *layout, 0, buffer);
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

Result<void> validate_column(const Column& column, const Field& field,
                             const std::string& name) {
    const Type& type = field.type;
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
    if (field.dictionary) {
        if (!column.offsets.empty()) {
            return std::unexpected(make_error(
                Code::BadHeader, "column " + name + " carries offsets for an index column"));
        }
        const std::size_t index_bytes = static_cast<std::size_t>(column.length) * 4;
        if (column.data.size() != index_bytes) {
            return std::unexpected(make_error(
                Code::BadHeader, "column " + name + " has the wrong index bytes"));
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
            if (schema.fields[index].dictionary) {
                // Index column: no offsets buffer, four bytes per row.
                data_needed = static_cast<std::uint64_t>(column.length) * 4;
            } else if (type.is_variable_length()) {
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
        auto valid = validate_column(column, schema.fields[index], schema.fields[index].name);
        if (!valid) return std::unexpected(valid.error());
        batch.columns.push_back(std::move(column));
    }
    return batch;
}

}  // namespace

// Decodes one DictionaryBatch message: the id at slot 0, isDelta at slot 2,
// and the values in a record batch body of exactly one column.
Result<DictionaryColumn> decode_dictionary_batch(
    const Schema& schema, const detail::FlatReader& reader,
    const detail::FlatReader::Table& table, std::span<const std::byte> payload,
    const std::shared_ptr<const std::vector<std::byte>>& owner, std::size_t base) {
    // flatbuffers omits a scalar equal to its default, and pyarrow writes id 0
    // as absent rather than as a zero. Absent therefore means 0, not an error.
    const std::int64_t id = reader.i64(table, 0).value_or(0);
    if (const auto delta = reader.u8(table, 2); delta && *delta != 0) {
        return std::unexpected(make_error(
            Code::Unsupported, "delta dictionaries are not supported"));
    }
    const auto data_pos = reader.target(table, 1);
    if (!data_pos) {
        return std::unexpected(
            make_error(Code::BadHeader, "dictionary batch carries no data"));
    }
    const auto data_table = reader.table_at(*data_pos);
    if (!data_table) {
        return std::unexpected(
            make_error(Code::BadHeader, "dictionary data is not a table"));
    }
    const Field* encoded = nullptr;
    for (const Field& field : schema.fields) {
        if (field.dictionary && field.dictionary->id == id) {
            encoded = &field;
            break;
        }
    }
    if (encoded == nullptr) {
        return std::unexpected(
            make_error(Code::BadHeader, "dictionary id is not in the schema"));
    }
    Schema values_schema;
    values_schema.fields.push_back(
        Field{"dictionary_values", encoded->type, false, std::nullopt});
    auto values =
        decode_record_batch(values_schema, reader, *data_table, payload, owner, base);
    if (!values) return std::unexpected(values.error());
    if (values->columns.size() != 1) {
        return std::unexpected(make_error(
            Code::BadHeader, "dictionary batch does not carry exactly one column"));
    }
    DictionaryColumn out;
    out.id = id;
    out.value_type = encoded->type;
    out.values = std::move(values->columns[0]);
    return out;
}

// Every index must point inside its dictionary, and every encoded field must
// have one.
Result<void> validate_dictionary_indices(const RecordBatch& batch) {
    for (std::size_t index = 0; index < batch.columns.size(); ++index) {
        const Field& field = batch.schema.fields[index];
        if (!field.dictionary) continue;
        const Column& column = batch.columns[index];
        std::int64_t limit = -1;
        for (const DictionaryColumn& dictionary : batch.dictionaries) {
            if (dictionary.id == field.dictionary->id) {
                limit = dictionary.values.length;
            }
        }
        if (limit < 0) {
            return std::unexpected(make_error(
                Code::BadHeader, "column " + field.name + " has no dictionary"));
        }
        for (std::int64_t row = 0; row < column.length; ++row) {
            if (!column.valid(row)) continue;
            const auto value = detail::load_le<std::int32_t>(
                column.data.bytes().data() + static_cast<std::size_t>(row) * 4);
            if (value < 0 || value >= limit) {
                return std::unexpected(make_error(
                    Code::BadHeader,
                    "column " + field.name + " has an index outside its dictionary"));
            }
        }
    }
    return {};
}

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
    std::vector<DictionaryColumn> dictionaries;
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
        if (*tag == detail::kMessageHeaderDictionaryBatch) {
            if (!have_schema) {
                return std::unexpected(make_error(
                    Code::BadHeader, "dictionary batch before the schema"));
            }
            auto dictionary = decode_dictionary_batch(
                schema, reader, *table, payload, owner, base + header->body_start);
            if (!dictionary) return std::unexpected(dictionary.error());
            dictionaries.push_back(std::move(*dictionary));
        } else if (*tag == detail::kMessageHeaderSchema) {
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
            batch->dictionaries = std::move(dictionaries);
            auto indexed = validate_dictionary_indices(*batch);
            if (!indexed) return std::unexpected(indexed.error());
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
    // A stream carries one dictionary per id, so the first batch's set is the
    // set for the whole stream. Replacement batches are rejected on read.
    merged.dictionaries = batches.front().dictionaries;
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
