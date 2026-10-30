#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "arrowlane/error.hpp"

namespace arrowlane {

struct Type {
    enum class Kind {
        Null,
        Bool,
        Int,
        Float,
        Utf8,
        LargeUtf8,
        Binary,
        LargeBinary,
        FixedSizeBinary,
        Decimal,
        Date,
        Time,
        Timestamp,
        Duration
    };

    Kind kind = Kind::Null;
    std::uint16_t bit_width = 0;
    bool is_signed = false;
    std::uint8_t float_bits = 0;
    std::uint8_t unit = 0;
    std::int32_t precision = 0;
    std::int32_t scale = 0;
    std::int32_t byte_width = 0;
    std::string timezone;

    [[nodiscard]] static Type null();
    [[nodiscard]] static Type boolean();
    [[nodiscard]] static Type integer(std::uint16_t bits, bool is_signed);
    [[nodiscard]] static Type floating(std::uint8_t bits);
    [[nodiscard]] static Type utf8();
    [[nodiscard]] static Type large_utf8();
    [[nodiscard]] static Type binary();
    [[nodiscard]] static Type large_binary();
    [[nodiscard]] static Type fixed_size_binary(std::int32_t width);
    [[nodiscard]] static Type decimal(std::int32_t precision, std::int32_t scale,
                                      std::uint16_t bits);
    [[nodiscard]] static Type date(std::uint8_t unit);
    [[nodiscard]] static Type time(std::uint8_t unit, std::uint16_t bits);
    [[nodiscard]] static Type timestamp(std::uint8_t unit, std::string timezone = {});
    [[nodiscard]] static Type duration(std::uint8_t unit);

    [[nodiscard]] bool is_variable_length() const noexcept;
    [[nodiscard]] bool has_offsets_64() const noexcept;
    [[nodiscard]] std::uint8_t offset_width() const noexcept;
    [[nodiscard]] std::uint8_t schema_tag() const noexcept;
    [[nodiscard]] bool json_supported() const noexcept;
    [[nodiscard]] std::size_t width() const noexcept;
    [[nodiscard]] std::string name() const;
};

struct Field {
    std::string name;
    Type type;
    bool nullable = true;
};

struct Schema {
    std::vector<Field> fields;
};

[[nodiscard]] bool operator==(const Type& left, const Type& right) noexcept;
[[nodiscard]] bool operator==(const Field& left, const Field& right) noexcept;
[[nodiscard]] bool operator==(const Schema& left, const Schema& right) noexcept;

[[nodiscard]] Result<void> validate_type(const Type& type);
[[nodiscard]] Result<void> validate_schema(const Schema& schema);

[[nodiscard]] Result<std::vector<std::byte>> encode_schema_message(const Schema& schema);
[[nodiscard]] Result<Schema> decode_schema_message(std::span<const std::byte> bytes);
[[nodiscard]] Result<std::string> schema_base64(const Schema& schema);
[[nodiscard]] Result<Schema> schema_from_base64(std::string_view text);

}  // namespace arrowlane
