#include "arrowlane/json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace arrowlane {
namespace {

void append_utf8(std::string& out, std::uint32_t code) {
    if (code <= 0x7F) {
        out.push_back(static_cast<char>(code));
    } else if (code <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

void write_string(std::string& out, std::string_view text) {
    out.push_back('"');
    for (char raw : text) {
        const auto byte = static_cast<unsigned char>(raw);
        switch (byte) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (byte < 0x20) {
                    char buffer[7];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", byte);
                    out += buffer;
                } else {
                    out.push_back(raw);
                }
                break;
        }
    }
    out.push_back('"');
}

void write_number(std::string& out, const JsonValue& value) {
    if (value.is_integer()) {
        char buffer[32];
        if (value.is_unsigned()) {
            const auto result =
                std::to_chars(buffer, buffer + sizeof(buffer), value.as_uint());
            out.append(buffer, result.ptr);
            return;
        }
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value.as_int());
        out.append(buffer, result.ptr);
        return;
    }
    const double number = value.as_number();
    if (std::isnan(number)) {
        out += "NaN";
        return;
    }
    if (std::isinf(number)) {
        out += number < 0 ? "-Infinity" : "Infinity";
        return;
    }
    char buffer[64];
    const auto result =
        std::to_chars(buffer, buffer + sizeof(buffer), number, std::chars_format::general);
    std::string text(buffer, result.ptr);
    if (text.find_first_of(".eE") == std::string::npos) {
        text += ".0";
    }
    out += text;
}

void write_value(std::string& out, const JsonValue& value, bool sort_keys) {
    switch (value.kind()) {
        case JsonValue::Kind::Null: out += "null"; break;
        case JsonValue::Kind::Bool: out += value.as_bool() ? "true" : "false"; break;
        case JsonValue::Kind::Number: write_number(out, value); break;
        case JsonValue::Kind::String: write_string(out, value.as_string()); break;
        case JsonValue::Kind::Array: {
            out.push_back('[');
            bool first = true;
            for (const auto& item : value.items()) {
                if (!first) out.push_back(',');
                first = false;
                write_value(out, item, sort_keys);
            }
            out.push_back(']');
            break;
        }
        case JsonValue::Kind::Object: {
            out.push_back('{');
            std::vector<std::size_t> order(value.fields().size());
            for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
            if (sort_keys) {
                std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
                    return value.fields()[left].first < value.fields()[right].first;
                });
            }
            bool first = true;
            for (const std::size_t index : order) {
                if (!first) out.push_back(',');
                first = false;
                write_string(out, value.fields()[index].first);
                out.push_back(':');
                write_value(out, value.fields()[index].second, sort_keys);
            }
            out.push_back('}');
            break;
        }
    }
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Result<JsonValue> parse() {
        skip_ws();
        auto value = parse_value(0);
        if (!value) return value;
        skip_ws();
        if (pos_ != text_.size()) {
            return std::unexpected(bad("trailing bytes"));
        }
        return value;
    }

private:
    [[nodiscard]] Error bad(const char* what) const {
        return make_error(Code::BadHeader,
                          std::string("json: ") + what + " at " + std::to_string(pos_));
    }

    void skip_ws() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++pos_;
        }
    }

    bool consume(char expected) {
        if (pos_ < text_.size() && text_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    Result<JsonValue> parse_literal(std::string_view name, JsonValue value) {
        if (text_.substr(pos_, name.size()) != name) {
            return std::unexpected(bad("unknown literal"));
        }
        pos_ += name.size();
        return value;
    }

    Result<JsonValue> parse_value(int depth) {
        if (depth > 64) return std::unexpected(bad("nesting too deep"));
        if (pos_ >= text_.size()) return std::unexpected(bad("unexpected end"));
        const char c = text_[pos_];
        if (c == '{') return parse_object(depth + 1);
        if (c == '[') return parse_array(depth + 1);
        if (c == '"') {
            auto text = parse_string();
            if (!text) return std::unexpected(text.error());
            return JsonValue(std::move(*text));
        }
        if (c == 't') return parse_literal("true", JsonValue(true));
        if (c == 'f') return parse_literal("false", JsonValue(false));
        if (c == 'n') return parse_literal("null", JsonValue());
        if (c == 'N') return parse_literal("NaN", JsonValue(std::nan("")));
        if (c == 'I') {
            return parse_literal("Infinity", JsonValue(std::numeric_limits<double>::infinity()));
        }
        if (c == '-' && text_.substr(pos_, 9) == "-Infinity") {
            return parse_literal("-Infinity",
                                 JsonValue(-std::numeric_limits<double>::infinity()));
        }
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        return std::unexpected(bad("unexpected byte"));
    }

    Result<JsonValue> parse_object(int depth) {
        JsonValue object = JsonValue::make_object();
        ++pos_;
        skip_ws();
        if (consume('}')) return object;
        while (true) {
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                return std::unexpected(bad("expected a key"));
            }
            auto key = parse_string();
            if (!key) return std::unexpected(key.error());
            skip_ws();
            if (!consume(':')) return std::unexpected(bad("expected a colon"));
            skip_ws();
            auto value = parse_value(depth);
            if (!value) return value;
            object.set(std::move(*key), std::move(*value));
            skip_ws();
            if (consume('}')) return object;
            if (!consume(',')) return std::unexpected(bad("expected a comma"));
            skip_ws();
        }
    }

    Result<JsonValue> parse_array(int depth) {
        JsonValue array = JsonValue::make_array();
        ++pos_;
        skip_ws();
        if (consume(']')) return array;
        while (true) {
            auto value = parse_value(depth);
            if (!value) return value;
            array.push(std::move(*value));
            skip_ws();
            if (consume(']')) return array;
            if (!consume(',')) return std::unexpected(bad("expected a comma"));
            skip_ws();
        }
    }

    bool parse_hex4(std::uint32_t& code) {
        if (pos_ + 4 > text_.size()) return false;
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index) {
            const char c = text_[pos_ + static_cast<std::size_t>(index)];
            std::uint32_t digit = 0;
            if (c >= '0' && c <= '9') {
                digit = static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                return false;
            }
            value = (value << 4) | digit;
        }
        pos_ += 4;
        code = value;
        return true;
    }

    Result<std::string> parse_string() {
        ++pos_;
        std::string out;
        while (pos_ < text_.size()) {
            const auto byte = static_cast<unsigned char>(text_[pos_++]);
            if (byte == '"') return out;
            if (byte == '\\') {
                if (pos_ >= text_.size()) return std::unexpected(bad("unterminated escape"));
                const char escape = text_[pos_++];
                switch (escape) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        std::uint32_t code = 0;
                        if (!parse_hex4(code)) {
                            return std::unexpected(bad("invalid unicode escape"));
                        }
                        if (code >= 0xD800 && code <= 0xDBFF) {
                            if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' ||
                                text_[pos_ + 1] != 'u') {
                                return std::unexpected(bad("lone high surrogate"));
                            }
                            pos_ += 2;
                            std::uint32_t low = 0;
                            if (!parse_hex4(low) || low < 0xDC00 || low > 0xDFFF) {
                                return std::unexpected(bad("invalid low surrogate"));
                            }
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        } else if (code >= 0xDC00 && code <= 0xDFFF) {
                            return std::unexpected(bad("lone low surrogate"));
                        }
                        append_utf8(out, code);
                        break;
                    }
                    default: return std::unexpected(bad("unknown escape"));
                }
            } else if (byte < 0x20) {
                return std::unexpected(bad("control character in string"));
            } else {
                out.push_back(static_cast<char>(byte));
            }
        }
        return std::unexpected(bad("unterminated string"));
    }

    Result<JsonValue> parse_number() {
        const std::size_t start = pos_;
        if (text_[pos_] == '-') ++pos_;
        if (pos_ >= text_.size()) return std::unexpected(bad("truncated number"));
        if (text_[pos_] == '0') {
            ++pos_;
        } else {
            if (text_[pos_] < '1' || text_[pos_] > '9') {
                return std::unexpected(bad("invalid number"));
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        bool is_float = false;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            is_float = true;
            ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                return std::unexpected(bad("invalid fraction"));
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            is_float = true;
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                return std::unexpected(bad("invalid exponent"));
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        const std::string_view token = text_.substr(start, pos_ - start);
        if (!is_float) {
            std::int64_t integer = 0;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), integer);
            if (result.ec == std::errc{} && result.ptr == token.data() + token.size()) {
                return JsonValue(integer);
            }
        }
        double number = 0.0;
        const auto result = std::from_chars(token.data(), token.data() + token.size(), number);
        if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
            return std::unexpected(bad("number out of range"));
        }
        return JsonValue(number);
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

}  // namespace

JsonValue::JsonValue(bool value) : kind_(Kind::Bool), bool_(value) {}

JsonValue::JsonValue(std::int64_t value)
    : kind_(Kind::Number), integer_(true), int_(value), number_(static_cast<double>(value)) {}

JsonValue::JsonValue(double value) : kind_(Kind::Number), number_(value) {}

JsonValue::JsonValue(std::string value) : kind_(Kind::String), string_(std::move(value)) {}

JsonValue JsonValue::make_array() {
    JsonValue value;
    value.kind_ = Kind::Array;
    return value;
}

JsonValue JsonValue::make_uint(std::uint64_t value) {
    JsonValue result;
    result.kind_ = Kind::Number;
    result.integer_ = true;
    result.unsigned_ = true;
    result.uint_ = value;
    result.number_ = static_cast<double>(value);
    return result;
}

JsonValue JsonValue::make_object() {
    JsonValue value;
    value.kind_ = Kind::Object;
    return value;
}

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
    for (const auto& field : fields_) {
        if (field.first == key) return &field.second;
    }
    return nullptr;
}

void JsonValue::set(std::string key, JsonValue value) {
    if (kind_ != Kind::Object) {
        kind_ = Kind::Object;
        fields_.clear();
    }
    for (auto& field : fields_) {
        if (field.first == key) {
            field.second = std::move(value);
            return;
        }
    }
    fields_.emplace_back(std::move(key), std::move(value));
}

void JsonValue::push(JsonValue value) {
    if (kind_ != Kind::Array) {
        kind_ = Kind::Array;
        items_.clear();
    }
    items_.push_back(std::move(value));
}

Result<JsonValue> json_parse(std::string_view text) { return Parser(text).parse(); }

std::string json_write(const JsonValue& value) {
    std::string out;
    write_value(out, value, false);
    return out;
}

std::string json_write_sorted(const JsonValue& value) {
    std::string out;
    write_value(out, value, true);
    return out;
}

}  // namespace arrowlane
