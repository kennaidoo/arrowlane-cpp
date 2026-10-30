#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace arrowlane::detail {

template <class T>
[[nodiscard]] bool is_aligned(std::span<const std::byte> bytes) noexcept {
    return reinterpret_cast<std::uintptr_t>(bytes.data()) % alignof(T) == 0;
}

#if defined(__cpp_lib_start_lifetime_as) && __cpp_lib_start_lifetime_as >= 202207L
// std::start_lifetime_as_array, cppreference.com/cpp/numeric/start_lifetime_as
template <class T>
[[nodiscard]] std::span<const T> view_as(std::span<const std::byte> bytes) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    if (bytes.size() % sizeof(T) != 0 || !is_aligned<T>(bytes)) return {};
    const std::size_t count = bytes.size() / sizeof(T);
    return {std::start_lifetime_as_array<T>(bytes.data(), count), count};
}
#else
template <class T>
[[nodiscard]] std::span<const T> view_as(std::span<const std::byte>) noexcept {
    return {};
}
#endif

template <class T>
[[nodiscard]] T read_at(std::span<const std::byte> bytes, std::size_t index) noexcept {
    T value{};
    std::memcpy(&value, bytes.data() + index * sizeof(T), sizeof(T));
    return value;
}

}  // namespace arrowlane::detail
