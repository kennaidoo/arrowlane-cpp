#include "arrowlane/lanes.hpp"

#include "detail/lanes_internal.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "arrowlane/base64.hpp"
#include "arrowlane/digest.hpp"
#include "arrowlane/json.hpp"
#include "arrowlane/schema.hpp"
#include "arrowlane/version.hpp"
#include "detail/bytes.hpp"
#include "detail/decimal.hpp"

namespace arrowlane {
namespace {

constexpr std::size_t kMaxLineBytes = 1'000'000;

Error bad_row(const std::string& name, std::size_t row, const std::string& what) {
    return make_error(Code::BadHeader, "column " + name + " row " +
                                           std::to_string(row) + " " + what);
}

std::int64_t floor_div(std::int64_t left, std::int64_t right) {
    std::int64_t quotient = left / right;
    if ((left % right != 0) && ((left < 0) != (right < 0))) --quotient;
    return quotient;
}

struct Civil {
    int year = 1970;
    unsigned month = 1;
    unsigned day = 1;
};

Civil civil_from_days(std::int64_t days) {
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const std::int64_t doe = days - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t year = yoe + era * 400;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const std::int64_t day = doy - (153 * mp + 2) / 5 + 1;
    const std::int64_t month = mp < 10 ? mp + 3 : mp - 9;
    Civil civil;
    civil.year = static_cast<int>(month <= 2 ? year + 1 : year);
    civil.month = static_cast<unsigned>(month);
    civil.day = static_cast<unsigned>(day);
    return civil;
}

std::int64_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2 ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const std::int64_t yoe = year - era * 400;
    const std::int64_t doy =
        (153 * (static_cast<std::int64_t>(month) + (month > 2 ? -3 : 9)) + 2) / 5 +
        static_cast<std::int64_t>(day) - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

std::string date_text(std::int64_t days) {
    const Civil civil = civil_from_days(days);
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", civil.year, civil.month,
                  civil.day);
    return buffer;
}

std::string time_text(std::int64_t seconds, std::int64_t micros) {
    const int hour = static_cast<int>(seconds / 3600);
    const int minute = static_cast<int>((seconds / 60) % 60);
    const int second = static_cast<int>(seconds % 60);
    const int fraction = static_cast<int>(micros);
    char buffer[64];
    if (fraction == 0) {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hour, minute, second);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%06d", hour, minute, second,
                      fraction);
    }
    return buffer;
}

Result<void> split_unit(std::int64_t value, std::uint8_t unit, std::int64_t& seconds,
                        std::int64_t& micros) {
    switch (unit) {
        case 0:
            seconds = value;
            micros = 0;
            break;
        case 1:
            seconds = floor_div(value, 1'000);
            micros = (value - seconds * 1'000) * 1'000;
            break;
        case 2:
            seconds = floor_div(value, 1'000'000);
            micros = value - seconds * 1'000'000;
            break;
        case 3:
            seconds = floor_div(value, 1'000'000'000);
            micros = (value - seconds * 1'000'000'000) / 1'000;
            break;
        default:
            return std::unexpected(make_error(Code::Invalid, "unit must be 0..3"));
    }
    return {};
}

std::string timestamp_text(std::int64_t seconds, std::int64_t micros,
                           const std::string& timezone) {
    const std::int64_t days = floor_div(seconds, 86'400);
    const std::int64_t rest = seconds - days * 86'400;
    std::string out = date_text(days);
    out.push_back('T');
    out += time_text(rest, micros);
    if (!timezone.empty()) {
        out += "+00:00";
    }
    return out;
}

JsonValue tagged(const char* tag, std::string text) {
    JsonValue value = JsonValue::make_object();
    value.set("$", JsonValue(std::string(tag)));
    value.set("v", JsonValue(std::move(text)));
    return value;
}

Result<JsonValue> encode_value(const Type& type, const Column& column,
                               std::int64_t row) {
    const std::size_t index = static_cast<std::size_t>(row);
    const std::size_t width = type.width();
    switch (type.kind) {
        case Type::Kind::Null: return JsonValue();
        case Type::Kind::Bool: {
            const std::byte byte = column.data[index / 8];
            const bool set = (byte & (std::byte{1} << (index % 8))) != std::byte{0};
            return JsonValue(set);
        }
        case Type::Kind::Int: {
            const auto raw = column.data.bytes().subspan(index * width, width);
            if (!type.is_signed) {
                std::uint64_t value = 0;
                switch (width) {
                    case 1: value = detail::load_le<std::uint8_t>(raw.data()); break;
                    case 2: value = detail::load_le<std::uint16_t>(raw.data()); break;
                    case 4: value = detail::load_le<std::uint32_t>(raw.data()); break;
                    default: value = detail::load_le<std::uint64_t>(raw.data()); break;
                }
                return JsonValue::make_uint(value);
            }
            std::int64_t value = 0;
            switch (width) {
                case 1: value = detail::load_le<std::int8_t>(raw.data()); break;
                case 2: value = detail::load_le<std::int16_t>(raw.data()); break;
                case 4: value = detail::load_le<std::int32_t>(raw.data()); break;
                default: value = detail::load_le<std::int64_t>(raw.data()); break;
            }
            return JsonValue(value);
        }
        case Type::Kind::Float: {
            double value = 0.0;
            if (type.float_bits == 32) {
                value = detail::load_le<float>(column.data.bytes().data() + index * 4);
            } else {
                value = detail::load_le<double>(column.data.bytes().data() + index * 8);
            }
            if (!std::isfinite(value)) return JsonValue();
            return JsonValue(value);
        }
        case Type::Kind::Utf8:
        case Type::Kind::LargeUtf8: {
            const auto bytes = column.bytes_at(row);
            std::string text(bytes.size(), '\0');
            if (!bytes.empty()) {
                std::memcpy(text.data(), bytes.data(), bytes.size());
            }
            return JsonValue(std::move(text));
        }
        case Type::Kind::Binary:
        case Type::Kind::LargeBinary: {
            const auto bytes = column.bytes_at(row);
            return tagged("bin", base64_encode(bytes));
        }
        case Type::Kind::FixedSizeBinary: {
            const auto bytes = column.data.bytes().subspan(index * width, width);
            return tagged("bin", base64_encode(bytes));
        }
        case Type::Kind::Decimal: {
            const auto bytes = column.data.bytes().subspan(index * width, width);
            const std::span<const std::byte, 16> fixed(bytes.data(), 16);
            return tagged("dec", detail::decimal128_to_text(fixed, type.scale));
        }
        case Type::Kind::Date: {
            std::int64_t days = 0;
            if (type.unit == 0) {
                days = detail::load_le<std::int32_t>(column.data.bytes().data() + index * 4);
            } else {
                days = floor_div(
                    detail::load_le<std::int64_t>(column.data.bytes().data() + index * 8),
                    86'400'000);
            }
            return tagged("date", date_text(days));
        }
        case Type::Kind::Time: {
            const std::int64_t raw =
                width == 4
                    ? detail::load_le<std::int32_t>(column.data.bytes().data() + index * 4)
                    : detail::load_le<std::int64_t>(column.data.bytes().data() + index * 8);
            std::int64_t seconds = 0;
            std::int64_t micros = 0;
            auto split = split_unit(raw, type.unit, seconds, micros);
            if (!split) return std::unexpected(split.error());
            return tagged("time", time_text(seconds, micros));
        }
        case Type::Kind::Timestamp: {
            if (!type.timezone.empty() && type.timezone != "UTC") {
                return std::unexpected(make_error(
                    Code::Unsupported,
                    "json lane accepts only an empty or UTC timezone"));
            }
            const std::int64_t raw =
                detail::load_le<std::int64_t>(column.data.bytes().data() + index * 8);
            std::int64_t seconds = 0;
            std::int64_t micros = 0;
            auto split = split_unit(raw, type.unit, seconds, micros);
            if (!split) return std::unexpected(split.error());
            return tagged("ts", timestamp_text(seconds, micros, type.timezone));
        }
        case Type::Kind::Duration: {
            const std::int64_t raw =
                detail::load_le<std::int64_t>(column.data.bytes().data() + index * 8);
            double divisor = 1.0;
            switch (type.unit) {
                case 0: divisor = 1.0; break;
                case 1: divisor = 1'000.0; break;
                case 2: divisor = 1'000'000.0; break;
                default: divisor = 1'000'000'000.0; break;
            }
            JsonValue value = JsonValue::make_object();
            value.set("$", JsonValue(std::string("dur")));
            value.set("v", JsonValue(static_cast<double>(raw) / divisor));
            return value;
        }
    }
    return std::unexpected(make_error(Code::Unsupported, "unknown type"));
}

Result<std::string> encode_json_payload(const RecordBatch& batch) {
    for (const Field& field : batch.schema.fields) {
        if (!field.type.json_supported()) {
            return std::unexpected(make_error(
                Code::Unsupported, "json lane cannot encode " + field.type.name()));
        }
    }
    std::string payload;
    payload.reserve(static_cast<std::size_t>(batch.rows()) * 64);
    for (std::int64_t row = 0; row < batch.rows(); ++row) {
        JsonValue object = JsonValue::make_object();
        for (std::size_t column_index = 0; column_index < batch.schema.fields.size();
             ++column_index) {
            const Field& field = batch.schema.fields[column_index];
            const Column& column = batch.columns[column_index];
            if (!column.valid(row)) {
                object.set(field.name, JsonValue());
                continue;
            }
            auto value = encode_value(field.type, column, row);
            if (!value) return std::unexpected(value.error());
            object.set(field.name, std::move(*value));
        }
        payload += json_write(object);
        if (row + 1 < batch.rows()) payload.push_back('\n');
    }
    return payload;
}

struct Cursor {
    std::string_view text;
    std::size_t pos = 0;

    [[nodiscard]] bool done() const noexcept { return pos >= text.size(); }
    [[nodiscard]] bool take(char symbol) {
        if (!done() && text[pos] == symbol) {
            ++pos;
            return true;
        }
        return false;
    }
    [[nodiscard]] Result<std::int64_t> take_digits(std::size_t count) {
        if (pos + count > text.size()) {
            return std::unexpected(make_error(Code::BadHeader, "value is too short"));
        }
        std::int64_t value = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const char symbol = text[pos + index];
            if (symbol < '0' || symbol > '9') {
                return std::unexpected(make_error(Code::BadHeader, "value carries a non digit"));
            }
            value = value * 10 + (symbol - '0');
        }
        pos += count;
        return value;
    }
};

Result<void> parse_time_of_day(Cursor& cursor, std::int64_t& seconds,
                               std::int64_t& micros) {
    auto hour = cursor.take_digits(2);
    if (!hour) return std::unexpected(hour.error());
    if (!cursor.take(':')) {
        return std::unexpected(make_error(Code::BadHeader, "time is not HH:MM:SS"));
    }
    auto minute = cursor.take_digits(2);
    if (!minute) return std::unexpected(minute.error());
    if (!cursor.take(':')) {
        return std::unexpected(make_error(Code::BadHeader, "time is not HH:MM:SS"));
    }
    auto second = cursor.take_digits(2);
    if (!second) return std::unexpected(second.error());
    if (*hour > 23 || *minute > 59 || *second > 60) {
        return std::unexpected(make_error(Code::BadHeader, "time is out of range"));
    }
    seconds = *hour * 3'600 + *minute * 60 + *second;
    micros = 0;
    if (cursor.take('.')) {
        std::int64_t scale = 100'000;
        std::size_t digits = 0;
        while (!cursor.done() && cursor.text[cursor.pos] >= '0' &&
               cursor.text[cursor.pos] <= '9') {
            if (digits < 6) {
                micros += (cursor.text[cursor.pos] - '0') * scale;
                scale /= 10;
                ++digits;
            }
            ++cursor.pos;
        }
        if (digits == 0) {
            return std::unexpected(make_error(Code::BadHeader, "time has an empty fraction"));
        }
    }
    return {};
}

Result<void> parse_date(Cursor& cursor, std::int64_t& days) {
    auto year = cursor.take_digits(4);
    if (!year) return std::unexpected(year.error());
    if (!cursor.take('-')) {
        return std::unexpected(make_error(Code::BadHeader, "date is not YYYY-MM-DD"));
    }
    auto month = cursor.take_digits(2);
    if (!month) return std::unexpected(month.error());
    if (!cursor.take('-')) {
        return std::unexpected(make_error(Code::BadHeader, "date is not YYYY-MM-DD"));
    }
    auto day = cursor.take_digits(2);
    if (!day) return std::unexpected(day.error());
    if (*month < 1 || *month > 12 || *day < 1 || *day > 31) {
        return std::unexpected(make_error(Code::BadHeader, "date is out of range"));
    }
    days = days_from_civil(static_cast<int>(*year), static_cast<unsigned>(*month),
                           static_cast<unsigned>(*day));
    return {};
}

Result<std::int64_t> parse_timestamp(const std::string& text, std::uint8_t unit,
                                     const std::string& timezone) {
    Cursor cursor{text, 0};
    std::int64_t days = 0;
    auto date = parse_date(cursor, days);
    if (!date) return std::unexpected(date.error());
    if (!cursor.take('T') && !cursor.take(' ')) {
        return std::unexpected(make_error(Code::BadHeader, "timestamp is not ISO 8601"));
    }
    std::int64_t seconds = 0;
    std::int64_t micros = 0;
    auto time = parse_time_of_day(cursor, seconds, micros);
    if (!time) return std::unexpected(time.error());
    if (!cursor.done()) {
        if (cursor.take('Z')) {
            // accepted only for a UTC column
            if (timezone != "UTC") {
                return std::unexpected(make_error(
                    Code::BadHeader, "timestamp carries a timezone the column lacks"));
            }
        } else if (cursor.take('+')) {
            auto hour = cursor.take_digits(2);
            if (!hour) return std::unexpected(hour.error());
            (void)cursor.take(':');
            auto minute = cursor.take_digits(2);
            if (!minute) return std::unexpected(minute.error());
            if (timezone != "UTC" || *hour != 0 || *minute != 0) {
                return std::unexpected(make_error(
                    Code::Unsupported, "json lane accepts only an empty or UTC timezone"));
            }
        } else {
            return std::unexpected(make_error(Code::BadHeader,
                                              "timestamp has a trailing suffix"));
        }
        if (!cursor.done()) {
            return std::unexpected(make_error(Code::BadHeader,
                                              "timestamp has a trailing suffix"));
        }
    } else if (!timezone.empty()) {
        return std::unexpected(make_error(Code::BadHeader,
                                          "timestamp lacks the +00:00 offset"));
    }
    const std::int64_t total = days * 86'400 + seconds;
    switch (unit) {
        case 0: return total;
        case 1: return total * 1'000 + micros / 1'000;
        case 2: return total * 1'000'000 + micros;
        default: return total * 1'000'000'000 + micros * 1'000;
    }
}

const JsonValue* tagged_value(const JsonValue& value, const char* tag) {
    if (!value.is_object()) return nullptr;
    const JsonValue* name = value.find("$");
    if (!name || name->kind() != JsonValue::Kind::String) return nullptr;
    if (name->as_string() != tag) return nullptr;
    return value.find("v");
}

bool fits_signed(std::int64_t value, std::uint16_t bits) noexcept {
    switch (bits) {
        case 8: return value >= -128 && value <= 127;
        case 16: return value >= -32'768 && value <= 32'767;
        case 32: return value >= -2'147'483'648LL && value <= 2'147'483'647LL;
        default: return true;
    }
}

bool fits_unsigned(std::uint64_t value, std::uint16_t bits) noexcept {
    switch (bits) {
        case 8: return value <= 255;
        case 16: return value <= 65'535;
        case 32: return value <= 4'294'967'295ull;
        default: return true;
    }
}

Result<void> decode_value(const Field& field, const JsonValue& value,
                          ColumnBuilder& builder, std::size_t row) {
    const Type& type = field.type;
    if (value.is_null() && type.kind != Type::Kind::Null) {
        builder.append_null();
        return {};
    }
    switch (type.kind) {
        case Type::Kind::Null:
            if (!value.is_null()) {
                return std::unexpected(bad_row(field.name, row, "is not null"));
            }
            builder.append_null();
            return {};
        case Type::Kind::Bool:
            if (value.kind() != JsonValue::Kind::Bool) {
                return std::unexpected(bad_row(field.name, row, "is not a boolean"));
            }
            builder.append_bool(value.as_bool());
            return {};
        case Type::Kind::Int: {
            if (value.kind() != JsonValue::Kind::Number || !value.is_integer()) {
                return std::unexpected(bad_row(field.name, row, "is not an integer"));
            }
            if (!type.is_signed) {
                if (value.is_unsigned()) {
                    if (!fits_unsigned(value.as_uint(), type.bit_width)) {
                        return std::unexpected(bad_row(field.name, row, "overflows"));
                    }
                    builder.append_uint(value.as_uint());
                    return {};
                }
                if (value.as_int() < 0 ||
                    !fits_unsigned(static_cast<std::uint64_t>(value.as_int()),
                                   type.bit_width)) {
                    return std::unexpected(bad_row(field.name, row, "overflows"));
                }
                builder.append_uint(static_cast<std::uint64_t>(value.as_int()));
                return {};
            }
            const std::int64_t raw =
                value.is_unsigned() ? static_cast<std::int64_t>(value.as_uint())
                                    : value.as_int();
            if (!fits_signed(raw, type.bit_width)) {
                return std::unexpected(bad_row(field.name, row, "overflows"));
            }
            builder.append_int(raw);
            return {};
        }
        case Type::Kind::Float: {
            if (value.kind() != JsonValue::Kind::Number) {
                return std::unexpected(bad_row(field.name, row, "is not a number"));
            }
            const double raw =
                value.is_integer()
                    ? (value.is_unsigned() ? static_cast<double>(value.as_uint())
                                           : static_cast<double>(value.as_int()))
                    : value.as_number();
            builder.append_double(raw);
            return {};
        }
        case Type::Kind::Utf8:
        case Type::Kind::LargeUtf8:
            if (value.kind() != JsonValue::Kind::String) {
                return std::unexpected(bad_row(field.name, row, "is not a string"));
            }
            builder.append_bytes(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(value.as_string().data()),
                value.as_string().size()));
            return {};
        case Type::Kind::Binary:
        case Type::Kind::LargeBinary:
        case Type::Kind::FixedSizeBinary: {
            const JsonValue* raw = tagged_value(value, "bin");
            if (!raw || raw->kind() != JsonValue::Kind::String) {
                return std::unexpected(bad_row(field.name, row, "is not a binary tag"));
            }
            auto bytes = base64_decode(raw->as_string());
            if (!bytes) {
                return std::unexpected(bad_row(field.name, row, "has invalid base64"));
            }
            builder.append_bytes(*bytes);
            return {};
        }
        case Type::Kind::Decimal: {
            const JsonValue* raw = tagged_value(value, "dec");
            if (!raw || raw->kind() != JsonValue::Kind::String) {
                return std::unexpected(bad_row(field.name, row, "is not a decimal tag"));
            }
            auto bytes = detail::decimal128_from_text(raw->as_string(), type.scale);
            if (!bytes) {
                return std::unexpected(bad_row(field.name, row, "is not a decimal128"));
            }
            builder.append_decimal(*bytes);
            return {};
        }
        case Type::Kind::Date: {
            const JsonValue* raw = tagged_value(value, "date");
            if (!raw || raw->kind() != JsonValue::Kind::String) {
                return std::unexpected(bad_row(field.name, row, "is not a date tag"));
            }
            Cursor cursor{raw->as_string(), 0};
            std::int64_t days = 0;
            auto parsed = parse_date(cursor, days);
            if (!parsed || !cursor.done()) {
                return std::unexpected(bad_row(field.name, row, "is not a date"));
            }
            if (type.unit == 0) {
                builder.append_int(days);
            } else {
                builder.append_int(days * 86'400'000);
            }
            return {};
        }
        case Type::Kind::Time: {
            const JsonValue* raw = tagged_value(value, "time");
            if (!raw || raw->kind() != JsonValue::Kind::String) {
                return std::unexpected(bad_row(field.name, row, "is not a time tag"));
            }
            Cursor cursor{raw->as_string(), 0};
            std::int64_t seconds = 0;
            std::int64_t micros = 0;
            auto parsed = parse_time_of_day(cursor, seconds, micros);
            if (!parsed || !cursor.done()) {
                return std::unexpected(bad_row(field.name, row, "is not a time"));
            }
            switch (type.unit) {
                case 0: builder.append_int(seconds); break;
                case 1: builder.append_int(seconds * 1'000 + micros / 1'000); break;
                case 2: builder.append_int(seconds * 1'000'000 + micros); break;
                default: builder.append_int(seconds * 1'000'000'000 + micros * 1'000); break;
            }
            return {};
        }
        case Type::Kind::Timestamp: {
            const JsonValue* raw = tagged_value(value, "ts");
            if (!raw || raw->kind() != JsonValue::Kind::String) {
                return std::unexpected(bad_row(field.name, row, "is not a timestamp tag"));
            }
            auto parsed =
                parse_timestamp(raw->as_string(), type.unit, type.timezone);
            if (!parsed) {
                return std::unexpected(bad_row(field.name, row, "is not a timestamp"));
            }
            builder.append_int(*parsed);
            return {};
        }
        case Type::Kind::Duration: {
            const JsonValue* raw = tagged_value(value, "dur");
            if (!raw || raw->kind() != JsonValue::Kind::Number) {
                return std::unexpected(bad_row(field.name, row, "is not a duration tag"));
            }
            const double seconds = raw->is_integer()
                                       ? static_cast<double>(raw->as_int())
                                       : raw->as_number();
            double multiplier = 1.0;
            switch (type.unit) {
                case 0: multiplier = 1.0; break;
                case 1: multiplier = 1'000.0; break;
                case 2: multiplier = 1'000'000.0; break;
                default: multiplier = 1'000'000'000.0; break;
            }
            builder.append_int(static_cast<std::int64_t>(
                std::llround(seconds * multiplier)));
            return {};
        }
    }
    return std::unexpected(make_error(Code::Unsupported, "unknown type"));
}

Result<RecordBatch> decode_json_payload(const Schema& schema, std::string_view payload) {
    for (const Field& field : schema.fields) {
        if (!field.type.json_supported()) {
            return std::unexpected(make_error(
                Code::Unsupported, "json lane cannot decode " + field.type.name()));
        }
    }
    std::vector<ColumnBuilder> builders;
    builders.reserve(schema.fields.size());
    for (const Field& field : schema.fields) {
        builders.emplace_back(field.type);
    }
    std::size_t rows = 0;
    std::size_t start = 0;
    while (start < payload.size()) {
        const std::size_t end = payload.find('\n', start);
        const std::size_t stop = end == std::string_view::npos ? payload.size() : end;
        const std::string_view line = payload.substr(start, stop - start);
        start = stop + 1;
        if (line.empty()) continue;
        if (line.size() > kMaxLineBytes) {
            return std::unexpected(make_error(
                Code::Limit, "payload line exceeds " + std::to_string(kMaxLineBytes)));
        }
        auto parsed = json_parse(line);
        if (!parsed) return std::unexpected(parsed.error());
        if (!parsed->is_object()) {
            return std::unexpected(make_error(Code::BadHeader,
                                              "payload row is not a JSON object"));
        }
        for (const Field& field : schema.fields) {
            if (parsed->find(field.name) == nullptr) {
                return std::unexpected(make_error(
                    Code::BadHeader,
                    "row is missing column '" + field.name + "'"));
            }
        }
        for (const auto& [key, item] : parsed->fields()) {
            (void)item;
            bool known = false;
            for (const Field& field : schema.fields) {
                if (field.name == key) known = true;
            }
            if (!known) {
                return std::unexpected(make_error(
                    Code::BadHeader, "row carries unknown column '" + key + "'"));
            }
        }
        for (std::size_t index = 0; index < schema.fields.size(); ++index) {
            const Field& field = schema.fields[index];
            const JsonValue* value = parsed->find(field.name);
            auto result = decode_value(field, *value, builders[index], rows);
            if (!result) return std::unexpected(result.error());
        }
        ++rows;
    }
    RecordBatch batch;
    batch.schema = schema;
    batch.columns.reserve(builders.size());
    for (auto& builder : builders) {
        batch.columns.push_back(builder.finish());
    }
    return batch;
}

}  // namespace

Result<Frame> detail::encode_json_lane(const RecordBatch& batch) {
    auto valid = validate_batch(batch);
    if (!valid) return std::unexpected(valid.error());
    auto payload = encode_json_payload(batch);
    if (!payload) return std::unexpected(payload.error());
    auto schema_message = encode_schema_message(batch.schema);
    if (!schema_message) return std::unexpected(schema_message.error());

    Frame frame;
    frame.lane = LaneId::Json;
    frame.payload.resize(payload->size());
    if (!payload->empty()) {
        std::memcpy(frame.payload.data(), payload->data(), payload->size());
    }
    frame.rows = static_cast<std::uint64_t>(batch.rows());
    frame.uncompressed_len = frame.payload.size();
    frame.flags = 0;
    JsonValue header = JsonValue::make_object();
    header.set("schema", JsonValue(base64_encode(*schema_message)));
    header.set("ncols", JsonValue(static_cast<std::int64_t>(batch.schema.fields.size())));
    header.set("producer",
               JsonValue(std::string("arrowlane-cpp/") + arrowlane::kVersion));
    header.set("media_type", JsonValue(std::string("application/x-ndjson")));
    frame.header = std::move(header);
    return frame;
}

Result<RecordBatch> detail::decode_json_lane(const Frame& frame,
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
    const JsonValue* schema_value = frame.header.find("schema");
    if (!schema_value || schema_value->kind() != JsonValue::Kind::String) {
        return std::unexpected(make_error(Code::BadHeader, "header carries no schema"));
    }
    auto bytes = base64_decode(schema_value->as_string());
    if (!bytes) {
        return std::unexpected(make_error(Code::BadHeader,
                                          "schema is not valid base64"));
    }
    auto schema = decode_schema_message(*bytes);
    if (!schema) return std::unexpected(schema.error());
    const std::string payload(reinterpret_cast<const char*>(frame.payload.data()),
                              frame.payload.size());
    auto batch = decode_json_payload(*schema, payload);
    if (!batch) return std::unexpected(batch.error());
    if (static_cast<std::uint64_t>(batch->rows()) != frame.rows) {
        return std::unexpected(make_error(Code::Frame,
                                          "frame rows do not match the payload"));
    }
    return batch;
}


}  // namespace arrowlane
