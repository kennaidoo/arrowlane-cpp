#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "arrowlane/schema.hpp"

namespace arrowlane::detail {

struct TypeTraits {
    std::string_view family;
    std::uint8_t schema_tag;
    std::uint8_t offset_width;
    bool variable_length;
};

inline constexpr std::array<TypeTraits, 14> kTypeTraits{{
    {"null", 1, 0, false},
    {"bool", 6, 0, false},
    {"int", 2, 0, false},
    {"float", 3, 0, false},
    {"utf8", 5, 4, true},
    {"large_utf8", 20, 8, true},
    {"binary", 4, 4, true},
    {"large_binary", 19, 8, true},
    {"fixed_size_binary", 15, 0, false},
    {"decimal", 7, 0, false},
    {"date", 8, 0, false},
    {"time", 9, 0, false},
    {"timestamp", 10, 0, false},
    {"duration", 18, 0, false},
}};

static_assert(kTypeTraits.size() ==
                  static_cast<std::size_t>(Type::Kind::Duration) + 1,
              "add a TypeTraits entry for every Type::Kind");

inline constexpr const TypeTraits& type_traits(Type::Kind kind) noexcept {
    return kTypeTraits[static_cast<std::size_t>(kind)];
}

inline std::optional<Type::Kind> kind_for_tag(std::uint8_t tag) noexcept {
    for (std::size_t index = 0; index < kTypeTraits.size(); ++index) {
        if (kTypeTraits[index].schema_tag == tag) {
            return static_cast<Type::Kind>(index);
        }
    }
    return std::nullopt;
}

}  // namespace arrowlane::detail
