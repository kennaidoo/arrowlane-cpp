#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace arrowlane::detail {

static_assert(std::endian::native == std::endian::little,
              "arrowlane requires a little endian host");

template <class T>
T load_le(const std::byte* source) noexcept {
    T value{};
    std::memcpy(&value, source, sizeof(T));
    return value;
}

template <class T>
void store_le(std::byte* target, T value) noexcept {
    std::memcpy(target, &value, sizeof(T));
}

inline std::byte* byte_ptr(void* pointer) noexcept {
    return static_cast<std::byte*>(pointer);
}

inline const std::byte* byte_ptr(const void* pointer) noexcept {
    return static_cast<const std::byte*>(pointer);
}

}  // namespace arrowlane::detail
