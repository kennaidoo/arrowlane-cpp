#include "arrowlane/schema.hpp"

#include <cstring>
#include <utility>
#include <vector>

#include "arrowlane/base64.hpp"
#include "detail/bytes.hpp"
#include "detail/flatbuf.hpp"
#include "detail/schema_tags.hpp"
#include "detail/type_traits.hpp"

namespace arrowlane {
namespace {

using detail::FlatReader;
using detail::FlatWriter;

std::size_t write_type_table(FlatWriter& writer, const Type& type) {
    switch (type.kind) {
        case Type::Kind::Null:
        case Type::Kind::Bool:
        case Type::Kind::Utf8:
        case Type::Kind::LargeUtf8:
        case Type::Kind::Binary:
        case Type::Kind::LargeBinary: {
            auto table = writer.begin_table(0);
            writer.end_table(table);
            return table.pos;
        }
        case Type::Kind::Int: {
            auto table = writer.begin_table(2);
            writer.add_i32(table, 0, static_cast<std::int32_t>(type.bit_width));
            writer.add_bool(table, 1, type.is_signed);
            writer.end_table(table);
            return table.pos;
        }
        case Type::Kind::Float: {
            auto table = writer.begin_table(1);
            const std::int16_t precision =
                type.float_bits == 16 ? 0 : (type.float_bits == 32 ? 1 : 2);
            writer.add_i16(table, 0, precision);
            writer.end_table(table);
            return table.pos;
        }
        case Type::Kind::Decimal: {
            auto table = writer.begin_table(3);
            writer.add_i32(table, 0, type.precision);
            writer.add_i32(table, 1, type.scale);
            writer.add_i32(table, 2, static_cast<std::int32_t>(type.bit_width));
            writer.end_table(table);
            return table.pos;
        }
        case Type::Kind::Date: {
            auto table = writer.begin_table(1);
            writer.add_i16(table, 0, static_cast<std::int16_t>(type.unit));
            writer.end_table(table);
            return table.pos;
        }
        case Type::Kind::Time: {
            auto table = writer.begin_table(2);
            writer.add_i16(table, 0, static_cast<std::int16_t>(type.unit));
            writer.add_i32(table, 1, static_cast<std::int32_t>(type.bit_width));
            writer.end_table(table);
            return table.pos;
        }
        case Type::Kind::Timestamp: {
            auto table = writer.begin_table(2);
            writer.add_i16(table, 0, static_cast<std::int16_t>(type.unit));
            if (type.timezone.empty()) {
                writer.end_table(table);
                return table.pos;
            }
            const std::size_t zone_field = writer.add_offset(table, 1);
            writer.end_table(table);
            const std::size_t zone = writer.add_string(type.timezone);
            writer.patch_offset(zone_field, zone);
            return table.pos;
        }
        case Type::Kind::Duration: {
            auto table = writer.begin_table(1);
            writer.add_i16(table, 0, static_cast<std::int16_t>(type.unit));
            writer.end_table(table);
            return table.pos;
        }
        case Type::Kind::FixedSizeBinary: {
            auto table = writer.begin_table(1);
            writer.add_i32(table, 0, type.byte_width);
            writer.end_table(table);
            return table.pos;
        }
    }
    return 0;
}

std::vector<std::byte> encapsulate(std::vector<std::byte> flat) {
    while (flat.size() % 8 != 0) {
        flat.push_back(std::byte{0});
    }
    std::vector<std::byte> out(flat.size() + 8);
    detail::store_le<std::uint32_t>(out.data(), 0xFFFFFFFFU);
    detail::store_le<std::uint32_t>(out.data() + 4, static_cast<std::uint32_t>(flat.size()));
    std::memcpy(out.data() + 8, flat.data(), flat.size());
    return out;
}

Error bad_header(const std::string& message) {
    return make_error(Code::BadHeader, message);
}

Result<Type> read_type(const FlatReader& reader, const FlatReader::Table& field_table,
                       std::uint8_t tag) {
    const auto kind = detail::kind_for_tag(tag);
    if (!kind) {
        return std::unexpected(make_error(Code::Unsupported,
                                          "schema uses an unsupported type: " +
                                              std::to_string(tag)));
    }
    switch (*kind) {
        case Type::Kind::Null: return Type::null();
        case Type::Kind::Bool: return Type::boolean();
        case Type::Kind::Utf8: return Type::utf8();
        case Type::Kind::LargeUtf8: return Type::large_utf8();
        case Type::Kind::Binary: return Type::binary();
        case Type::Kind::LargeBinary: return Type::large_binary();
        default: break;
    }
    const auto type_pos = reader.target(field_table, 3);
    if (!type_pos) {
        return std::unexpected(bad_header("schema field carries no type table"));
    }
    const auto table = reader.table_at(*type_pos);
    if (!table) {
        return std::unexpected(bad_header("type table is not a flatbuffer table"));
    }
    switch (*kind) {
        case Type::Kind::Int: {
            const auto bits = reader.i32(*table, 0);
            if (!bits) return std::unexpected(bad_header("int type carries no bit width"));
            const auto sign = reader.u8(*table, 1).value_or(0);
            return Type::integer(static_cast<std::uint16_t>(*bits), sign != 0);
        }
        case Type::Kind::Float: {
            const std::int16_t precision = reader.i16(*table, 0).value_or(0);
            if (precision < 0 || precision > 2) {
                return std::unexpected(bad_header("floating precision must be 0..2"));
            }
            const std::uint16_t bits = precision == 0 ? 16 : (precision == 1 ? 32 : 64);
            return Type::floating(static_cast<std::uint8_t>(bits));
        }
        case Type::Kind::Decimal: {
            const auto precision = reader.i32(*table, 0);
            const auto scale = reader.i32(*table, 1);
            if (!precision || !scale) {
                return std::unexpected(bad_header("decimal type needs precision and scale"));
            }
            const std::int32_t bits = reader.i32(*table, 2).value_or(128);
            return Type::decimal(*precision, *scale, static_cast<std::uint16_t>(bits));
        }
        case Type::Kind::Date: {
            const std::int16_t unit = reader.i16(*table, 0).value_or(1);
            return Type::date(static_cast<std::uint8_t>(unit));
        }
        case Type::Kind::Time: {
            const std::int16_t unit = reader.i16(*table, 0).value_or(1);
            const std::int32_t bits = reader.i32(*table, 1).value_or(32);
            return Type::time(static_cast<std::uint8_t>(unit),
                              static_cast<std::uint16_t>(bits));
        }
        case Type::Kind::Timestamp: {
            const std::int16_t unit = reader.i16(*table, 0).value_or(0);
            std::string zone;
            if (reader.field(*table, 1)) {
                const auto text = reader.string(*table, 1);
                if (!text) return std::unexpected(bad_header("timestamp zone is not a string"));
                zone = std::string(*text);
            }
            return Type::timestamp(static_cast<std::uint8_t>(unit), std::move(zone));
        }
        case Type::Kind::Duration: {
            const std::int16_t unit = reader.i16(*table, 0).value_or(1);
            return Type::duration(static_cast<std::uint8_t>(unit));
        }
        case Type::Kind::FixedSizeBinary: {
            const auto width = reader.i32(*table, 0);
            if (!width) {
                return std::unexpected(bad_header("fixed size binary type carries no width"));
            }
            return Type::fixed_size_binary(*width);
        }
        default: break;
    }
    return std::unexpected(
        make_error(Code::Unsupported, "schema field type is not readable"));
}

}  // namespace

Result<std::vector<std::byte>> encode_schema_message(const Schema& schema) {
    auto valid = validate_schema(schema);
    if (!valid) return std::unexpected(valid.error());
    std::size_t name_bytes = 0;
    for (const Field& field : schema.fields) {
        name_bytes += field.name.size();
    }
    FlatWriter writer(512 + 96 * schema.fields.size() + name_bytes);
    auto message = writer.begin_table(4);
    writer.add_i16(message, 0, detail::kMessageVersionV5);
    writer.add_u8(message, 1, detail::kMessageHeaderSchema);
    const std::size_t header_field = writer.add_offset(message, 2);
    writer.add_i64(message, 3, 0);
    writer.end_table(message);

    auto schema_table = writer.begin_table(2);
    const std::size_t fields_field = writer.add_offset(schema_table, 1);
    writer.end_table(schema_table);

    auto fields = writer.add_offset_vector(schema.fields.size());
    writer.patch_offset(fields_field, fields.pos);

    for (std::size_t index = 0; index < schema.fields.size(); ++index) {
        const Field& field = schema.fields[index];
        auto field_table = writer.begin_table(6);
        const std::size_t name_field = writer.add_offset(field_table, 0);
        writer.add_bool(field_table, 1, field.nullable);
        writer.add_u8(field_table, 2, field.type.schema_tag());
        const std::size_t type_field = writer.add_offset(field_table, 3);
        const std::size_t children_field = writer.add_offset(field_table, 5);
        writer.end_table(field_table);

        const std::size_t name_pos = writer.add_string(field.name);
        writer.patch_offset(name_field, name_pos);
        const std::size_t type_pos = write_type_table(writer, field.type);
        writer.patch_offset(type_field, type_pos);
        auto children = writer.add_offset_vector(0);
        writer.patch_offset(children_field, children.pos);
        writer.set_element(fields, index, field_table.pos);
    }

    writer.patch_offset(header_field, schema_table.pos);
    return encapsulate(writer.finish(message.pos));
}

Result<Schema> decode_schema_message(std::span<const std::byte> bytes) {
    if (bytes.size() < 8) {
        return std::unexpected(bad_header("schema message is too short"));
    }
    const std::uint32_t first = detail::load_le<std::uint32_t>(bytes.data());
    std::size_t meta_start = 0;
    std::uint32_t meta_len = 0;
    if (first == 0xFFFFFFFFU) {
        meta_len = detail::load_le<std::uint32_t>(bytes.data() + 4);
        meta_start = 8;
        if (meta_len % 8 != 0) {
            return std::unexpected(bad_header("metadata length is not a multiple of 8"));
        }
    } else {
        meta_len = first;
        meta_start = 4;
    }
    if (meta_len == 0) {
        return std::unexpected(bad_header("empty schema message"));
    }
    if (meta_start + static_cast<std::size_t>(meta_len) > bytes.size()) {
        return std::unexpected(make_error(
            Code::TruncatedFrame,
            "schema message declares " + std::to_string(meta_len) + " metadata bytes, carries " +
                std::to_string(bytes.size() - meta_start)));
    }
    const FlatReader reader(bytes.subspan(meta_start, meta_len));
    const auto message = reader.root();
    if (!message) {
        return std::unexpected(bad_header("schema message is not a flatbuffer message"));
    }
    const auto version = reader.i16(*message, 0);
    if (!version || *version != detail::kMessageVersionV5) {
        return std::unexpected(bad_header("schema message version must be V5"));
    }
    const auto tag = reader.u8(*message, 1);
    if (!tag || *tag != detail::kMessageHeaderSchema) {
        return std::unexpected(bad_header("schema message carries no schema"));
    }
    const auto schema_pos = reader.target(*message, 2);
    if (!schema_pos) {
        return std::unexpected(bad_header("schema message carries no schema table"));
    }
    const auto schema_table = reader.table_at(*schema_pos);
    if (!schema_table) {
        return std::unexpected(bad_header("schema table is not a flatbuffer table"));
    }
    const auto fields = reader.vector(*schema_table, 1);
    if (!fields) {
        return std::unexpected(bad_header("schema table carries no fields"));
    }
    Schema schema;
    schema.fields.reserve(fields->count);
    for (std::size_t index = 0; index < fields->count; ++index) {
        const auto field_pos = reader.element_target(*fields, index);
        if (!field_pos) {
            return std::unexpected(bad_header("schema field offset is invalid"));
        }
        const auto field_table = reader.table_at(*field_pos);
        if (!field_table) {
            return std::unexpected(bad_header("schema field is not a flatbuffer table"));
        }
        const auto name = reader.string(*field_table, 0);
        if (!name) {
            return std::unexpected(bad_header("schema field carries no name"));
        }
        const bool nullable = reader.u8(*field_table, 1).value_or(0) != 0;
        const auto type_tag = reader.u8(*field_table, 2);
        if (!type_tag) {
            return std::unexpected(bad_header("schema field carries no type tag"));
        }
        if (reader.field(*field_table, 4)) {
            return std::unexpected(make_error(
                Code::Unsupported, "dictionary encoding is not supported"));
        }
        auto type = read_type(reader, *field_table, *type_tag);
        if (!type) return std::unexpected(type.error());
        auto valid = validate_type(*type);
        if (!valid) return std::unexpected(valid.error());
        schema.fields.push_back(Field{std::string(*name), std::move(*type), nullable});
    }
    return schema;
}

Result<std::string> schema_base64(const Schema& schema) {
    auto message = encode_schema_message(schema);
    if (!message) return std::unexpected(message.error());
    return base64_encode(*message);
}

Result<Schema> schema_from_base64(std::string_view text) {
    auto bytes = base64_decode(text);
    if (!bytes) return std::unexpected(bytes.error());
    return decode_schema_message(*bytes);
}

}  // namespace arrowlane
