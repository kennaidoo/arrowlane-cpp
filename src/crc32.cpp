#include "arrowlane/crc32.hpp"

#include <array>

namespace arrowlane {
namespace {

consteval std::array<std::array<std::uint32_t, 256>, 8> make_tables() {
    std::array<std::array<std::uint32_t, 256>, 8> tables{};
    for (std::size_t index = 0; index < 256; ++index) {
        std::uint32_t value = static_cast<std::uint32_t>(index);
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1U) != 0U ? 0xEDB88320U ^ (value >> 1) : value >> 1;
        }
        tables[0][index] = value;
    }
    for (std::size_t slice = 1; slice < 8; ++slice) {
        for (std::size_t index = 0; index < 256; ++index) {
            const std::uint32_t previous = tables[slice - 1][index];
            tables[slice][index] = tables[0][previous & 0xFFU] ^ (previous >> 8);
        }
    }
    return tables;
}

constexpr auto kTables = make_tables();

}  // namespace

std::uint32_t crc32(std::span<const std::byte> bytes) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    std::size_t index = 0;
    for (; index + 8 <= bytes.size(); index += 8) {
        const std::uint32_t low =
            crc ^ (static_cast<std::uint32_t>(data[index]) |
                   (static_cast<std::uint32_t>(data[index + 1]) << 8) |
                   (static_cast<std::uint32_t>(data[index + 2]) << 16) |
                   (static_cast<std::uint32_t>(data[index + 3]) << 24));
        crc = kTables[7][low & 0xFFU] ^ kTables[6][(low >> 8) & 0xFFU] ^
              kTables[5][(low >> 16) & 0xFFU] ^ kTables[4][(low >> 24) & 0xFFU] ^
              kTables[3][data[index + 4]] ^ kTables[2][data[index + 5]] ^
              kTables[1][data[index + 6]] ^ kTables[0][data[index + 7]];
    }
    for (; index < bytes.size(); ++index) {
        crc = kTables[0][(crc ^ data[index]) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

}  // namespace arrowlane
