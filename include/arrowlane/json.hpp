#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "arrowlane/error.hpp"

namespace arrowlane {

class JsonValue {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    JsonValue() = default;
    explicit JsonValue(bool value);
    explicit JsonValue(std::int64_t value);
    explicit JsonValue(double value);
    explicit JsonValue(std::string value);

    static JsonValue make_array();
    static JsonValue make_object();
    static JsonValue make_uint(std::uint64_t value);

    [[nodiscard]]    Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool is_object() const noexcept { return kind_ == Kind::Object; }
    [[nodiscard]] bool is_null() const noexcept { return kind_ == Kind::Null; }
    [[nodiscard]] bool is_integer() const noexcept { return integer_; }
    [[nodiscard]] bool is_unsigned() const noexcept { return unsigned_; }
    [[nodiscard]] bool as_bool() const noexcept { return bool_; }
    [[nodiscard]] std::int64_t as_int() const noexcept { return int_; }
    [[nodiscard]] std::uint64_t as_uint() const noexcept { return uint_; }
    [[nodiscard]] double as_number() const noexcept { return number_; }
    [[nodiscard]] const std::string& as_string() const noexcept { return string_; }
    [[nodiscard]] const std::vector<JsonValue>& items() const noexcept { return items_; }
    [[nodiscard]] const std::vector<std::pair<std::string, JsonValue>>& fields() const noexcept {
        return fields_;
    }

    [[nodiscard]] const JsonValue* find(std::string_view key) const noexcept;
    void set(std::string key, JsonValue value);
    void push(JsonValue value);

private:
    Kind kind_ = Kind::Null;
    bool bool_ = false;
    bool integer_ = false;
    bool unsigned_ = false;
    std::int64_t int_ = 0;
    std::uint64_t uint_ = 0;
    double number_ = 0.0;
    std::string string_;
    std::vector<JsonValue> items_;
    std::vector<std::pair<std::string, JsonValue>> fields_;
};

[[nodiscard]] Result<JsonValue> json_parse(std::string_view text);

[[nodiscard]] std::string json_write(const JsonValue& value);

[[nodiscard]] std::string json_write_sorted(const JsonValue& value);

}  // namespace arrowlane
