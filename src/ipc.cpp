#include "arrowlane/ipc.hpp"

#include <array>
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

Result<void> place(std::vector<std::byte>& body, const IpcBuffer& target,
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

Result<std::vector<std::byte>> build_ipc_body(const RecordBatch& batch,
                                              const IpcLayout& layout) {
    if (layout.buffers.size() != expected_buffers(batch.schema)) {
        return std::unexpected(make_error(Code::Invalid,
                                          "ipc layout has the wrong buffer count"));
    }
    std::vector<std::byte> body(static_cast<std::size_t>(layout.body_size), std::byte{0});
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
    return body;
}

Result<std::vector<std::byte>> encode_ipc(const RecordBatch& batch) {
    auto schema_message = encode_schema_message(batch.schema);
    if (!schema_message) return std::unexpected(schema_message.error());
    auto layout = plan_ipc_body(batch);
    if (!layout) return std::unexpected(layout.error());
    auto body = build_ipc_body(batch, *layout);
    if (!body) return std::unexpected(body.error());

    detail::FlatWriter writer(1024 + body->size());
    auto message = writer.begin_table(4);
    writer.add_i16(message, 0, detail::kMessageVersionV5);
    writer.add_u8(message, 1, detail::kMessageHeaderRecordBatch);
    const std::size_t header_field = writer.add_offset(message, 2);
    writer.add_i64(message, 3, static_cast<std::int64_t>(body->size()));
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

    std::vector<std::byte> out = std::move(*schema_message);
    auto meta = detail::encapsulate(writer.finish(message.pos));
    out.insert(out.end(), meta.begin(), meta.end());
    out.insert(out.end(), body->begin(), body->end());
    out.insert(out.end(), kEndOfStream.begin(), kEndOfStream.end());
    return out;
}

}  // namespace arrowlane
