#include "arrowlane/schema.hpp"

#include <string>
#include <utility>

namespace arrowlane {
namespace {

const char* unit_name(std::uint8_t unit) noexcept {
    switch (unit) {
        case 0: return "s";
        case 1: return "ms";
        case 2: return "us";
        case 3: return "ns";
    }
    return "?";
}

}  // namespace

Type Type::null() {
    Type type;
    type.kind = Kind::Null;
    return type;
}

Type Type::boolean() {
    Type type;
    type.kind = Kind::Bool;
    return type;
}

Type Type::integer(std::uint16_t bits, bool is_signed_value) {
    Type type;
    type.kind = Kind::Int;
    type.bit_width = bits;
    type.is_signed = is_signed_value;
    return type;
}

Type Type::floating(std::uint8_t bits) {
    Type type;
    type.kind = Kind::Float;
    type.float_bits = bits;
    return type;
}

Type Type::utf8() {
    Type type;
    type.kind = Kind::Utf8;
    return type;
}

Type Type::large_utf8() {
    Type type;
    type.kind = Kind::LargeUtf8;
    return type;
}

Type Type::binary() {
    Type type;
    type.kind = Kind::Binary;
    return type;
}

Type Type::large_binary() {
    Type type;
    type.kind = Kind::LargeBinary;
    return type;
}

Type Type::fixed_size_binary(std::int32_t width) {
    Type type;
    type.kind = Kind::FixedSizeBinary;
    type.byte_width = width;
    return type;
}

Type Type::decimal(std::int32_t precision_value, std::int32_t scale_value, std::uint16_t bits) {
    Type type;
    type.kind = Kind::Decimal;
    type.precision = precision_value;
    type.scale = scale_value;
    type.bit_width = bits;
    return type;
}

Type Type::date(std::uint8_t date_unit) {
    Type type;
    type.kind = Kind::Date;
    type.unit = date_unit;
    return type;
}

Type Type::time(std::uint8_t time_unit, std::uint16_t bits) {
    Type type;
    type.kind = Kind::Time;
    type.unit = time_unit;
    type.bit_width = bits;
    return type;
}

Type Type::timestamp(std::uint8_t time_unit, std::string zone) {
    Type type;
    type.kind = Kind::Timestamp;
    type.unit = time_unit;
    type.timezone = std::move(zone);
    return type;
}

Type Type::duration(std::uint8_t time_unit) {
    Type type;
    type.kind = Kind::Duration;
    type.unit = time_unit;
    return type;
}

bool Type::is_variable_length() const noexcept {
    return kind == Kind::Utf8 || kind == Kind::LargeUtf8 || kind == Kind::Binary ||
           kind == Kind::LargeBinary;
}

bool Type::has_offsets_64() const noexcept {
    return kind == Kind::LargeUtf8 || kind == Kind::LargeBinary;
}

std::uint8_t Type::offset_width() const noexcept {
    return has_offsets_64() ? 8 : 4;
}

std::size_t Type::width() const noexcept {
    switch (kind) {
        case Kind::Null: return 0;
        case Kind::Bool: return 1;
        case Kind::Int: return static_cast<std::size_t>(bit_width) / 8;
        case Kind::Float: return static_cast<std::size_t>(float_bits) / 8;
        case Kind::Utf8:
        case Kind::LargeUtf8:
        case Kind::Binary:
        case Kind::LargeBinary: return 0;
        case Kind::FixedSizeBinary: return static_cast<std::size_t>(byte_width);
        case Kind::Decimal: return static_cast<std::size_t>(bit_width) / 8;
        case Kind::Date: return unit == 0 ? 4 : 8;
        case Kind::Time: return static_cast<std::size_t>(bit_width) / 8;
        case Kind::Timestamp:
        case Kind::Duration: return 8;
    }
    return 0;
}

std::string Type::name() const {
    switch (kind) {
        case Kind::Null: return "null";
        case Kind::Bool: return "bool";
        case Kind::Int:
            return std::string(is_signed ? "int" : "uint") + std::to_string(bit_width);
        case Kind::Float: return "float" + std::to_string(float_bits);
        case Kind::Utf8: return "utf8";
        case Kind::LargeUtf8: return "large_utf8";
        case Kind::Binary: return "binary";
        case Kind::LargeBinary: return "large_binary";
        case Kind::FixedSizeBinary:
            return "fixed_size_binary(" + std::to_string(byte_width) + ")";
        case Kind::Decimal:
            return "decimal" + std::to_string(bit_width) + "(" + std::to_string(precision) +
                   "," + std::to_string(scale) + ")";
        case Kind::Date: return unit == 0 ? "date32" : "date64";
        case Kind::Time: {
            const std::string base = bit_width == 32 ? "time32" : "time64";
            return base + "[" + unit_name(unit) + "]";
        }
        case Kind::Timestamp: {
            std::string text = std::string("timestamp[") + unit_name(unit);
            if (!timezone.empty()) {
                text += ",";
                text += timezone;
            }
            text += "]";
            return text;
        }
        case Kind::Duration:
            return std::string("duration[") + unit_name(unit) + "]";
    }
    return "unknown";
}

bool operator==(const Type& left, const Type& right) noexcept {
    return left.kind == right.kind && left.bit_width == right.bit_width &&
           left.is_signed == right.is_signed && left.float_bits == right.float_bits &&
           left.unit == right.unit && left.precision == right.precision &&
           left.scale == right.scale && left.byte_width == right.byte_width &&
           left.timezone == right.timezone;
}

bool operator==(const Field& left, const Field& right) noexcept {
    return left.name == right.name && left.type == right.type &&
           left.nullable == right.nullable;
}

bool operator==(const Schema& left, const Schema& right) noexcept {
    return left.fields == right.fields;
}

Result<void> validate_type(const Type& type) {
    switch (type.kind) {
        case Type::Kind::Int:
            if (type.bit_width != 8 && type.bit_width != 16 && type.bit_width != 32 &&
                type.bit_width != 64) {
                return std::unexpected(make_error(Code::Invalid, "int bit width must be 8, 16, 32 or 64"));
            }
            break;
        case Type::Kind::Float:
            if (type.float_bits != 16 && type.float_bits != 32 && type.float_bits != 64) {
                return std::unexpected(make_error(Code::Invalid, "float bits must be 16, 32 or 64"));
            }
            break;
        case Type::Kind::Decimal: {
            if (type.bit_width != 128 && type.bit_width != 256) {
                return std::unexpected(make_error(Code::Invalid, "decimal bit width must be 128 or 256"));
            }
            const std::int32_t max_precision = type.bit_width == 128 ? 38 : 76;
            if (type.precision < 1 || type.precision > max_precision) {
                return std::unexpected(make_error(
                    Code::Invalid,
                    "decimal precision must be 1.." + std::to_string(max_precision)));
            }
            if (type.scale < 0 || type.scale > type.precision) {
                return std::unexpected(make_error(Code::Invalid, "decimal scale must be 0..precision"));
            }
            break;
        }
        case Type::Kind::Date:
            if (type.unit > 1) {
                return std::unexpected(make_error(Code::Invalid, "date unit must be 0 or 1"));
            }
            break;
        case Type::Kind::Time:
            if (type.unit > 3) {
                return std::unexpected(make_error(Code::Invalid, "time unit must be 0..3"));
            }
            if (type.bit_width != 32 && type.bit_width != 64) {
                return std::unexpected(make_error(Code::Invalid, "time bit width must be 32 or 64"));
            }
            break;
        case Type::Kind::Timestamp:
        case Type::Kind::Duration:
            if (type.unit > 3) {
                return std::unexpected(make_error(Code::Invalid, "unit must be 0..3"));
            }
            break;
        case Type::Kind::FixedSizeBinary:
            if (type.byte_width <= 0) {
                return std::unexpected(make_error(Code::Invalid, "fixed size binary width must be positive"));
            }
            break;
        case Type::Kind::Null:
        case Type::Kind::Bool:
        case Type::Kind::Utf8:
        case Type::Kind::LargeUtf8:
        case Type::Kind::Binary:
        case Type::Kind::LargeBinary: break;
    }
    return {};
}

Result<void> validate_schema(const Schema& schema) {
    for (const Field& field : schema.fields) {
        auto valid = validate_type(field.type);
        if (!valid) return valid;
    }
    return {};
}

}  // namespace arrowlane
