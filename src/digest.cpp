#include "arrowlane/digest.hpp"

#include <cstddef>
#include <cstring>
#include <vector>

#include "arrowlane/crc32.hpp"
#include "detail/bytes.hpp"

namespace arrowlane {
namespace {

void append_le(std::vector<std::byte>& out, std::uint64_t value, std::size_t width) {
    for (std::size_t index = 0; index < width; ++index) {
        out.push_back(static_cast<std::byte>((value >> (8 * index)) & 0xFF));
    }
}

void append_bytes(std::vector<std::byte>& out, std::span<const std::byte> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

std::int64_t sign_extend(std::span<const std::byte> bytes) noexcept {
    switch (bytes.size()) {
        case 1: return detail::load_le<std::int8_t>(bytes.data());
        case 2: return detail::load_le<std::int16_t>(bytes.data());
        case 4: return detail::load_le<std::int32_t>(bytes.data());
        default: return detail::load_le<std::int64_t>(bytes.data());
    }
}

std::uint64_t zero_extend(std::span<const std::byte> bytes) noexcept {
    switch (bytes.size()) {
        case 1: return detail::load_le<std::uint8_t>(bytes.data());
        case 2: return detail::load_le<std::uint16_t>(bytes.data());
        case 4: return detail::load_le<std::uint32_t>(bytes.data());
        default: return detail::load_le<std::uint64_t>(bytes.data());
    }
}

}  // namespace

std::uint32_t column_digest(const Column& column, const Type& type) {
    std::vector<std::byte> stream;
    stream.reserve(static_cast<std::size_t>(column.length) * 16);
    const std::size_t width = type.width();
    for (std::int64_t row = 0; row < column.length; ++row) {
        const bool valid = column.valid(row);
        stream.push_back(valid ? std::byte{1} : std::byte{0});
        if (!valid) continue;
        const std::size_t index = static_cast<std::size_t>(row);
        switch (type.kind) {
            case Type::Kind::Null: break;
            case Type::Kind::Bool: {
                const std::byte byte = column.data[index / 8];
                const bool set =
                    (byte & (std::byte{1} << (index % 8))) != std::byte{0};
                stream.push_back(set ? std::byte{1} : std::byte{0});
                break;
            }
            case Type::Kind::Int: {
                const auto raw = column.data.bytes().subspan(index * width, width);
                const std::uint64_t value =
                    type.is_signed ? static_cast<std::uint64_t>(sign_extend(raw))
                                   : zero_extend(raw);
                append_le(stream, value, 8);
                break;
            }
            case Type::Kind::Float: {
                if (type.float_bits == 16) {
                    append_bytes(stream, column.data.bytes().subspan(index * 2, 2));
                } else if (type.float_bits == 32) {
                    const float narrow =
                        detail::load_le<float>(column.data.bytes().data() + index * 4);
                    const double wide = narrow;
                    std::uint64_t bits = 0;
                    std::memcpy(&bits, &wide, 8);
                    append_le(stream, bits, 8);
                } else {
                    append_bytes(stream, column.data.bytes().subspan(index * 8, 8));
                }
                break;
            }
            case Type::Kind::Utf8:
            case Type::Kind::Binary: {
                const auto value = column.bytes_at(row);
                append_le(stream, value.size(), 4);
                append_bytes(stream, value);
                break;
            }
            case Type::Kind::LargeUtf8:
            case Type::Kind::LargeBinary: {
                const auto value = column.bytes_at(row);
                append_le(stream, value.size(), 8);
                append_bytes(stream, value);
                break;
            }
            case Type::Kind::FixedSizeBinary: {
                const auto value = column.data.bytes().subspan(index * width, width);
                append_le(stream, width, 4);
                append_bytes(stream, value);
                break;
            }
            case Type::Kind::Decimal: {
                append_bytes(stream, column.data.bytes().subspan(index * width, width));
                break;
            }
            case Type::Kind::Date:
            case Type::Kind::Time: {
                if (width == 4) {
                    append_bytes(stream, column.data.bytes().subspan(index * 4, 4));
                } else {
                    append_bytes(stream, column.data.bytes().subspan(index * 8, 8));
                }
                break;
            }
            case Type::Kind::Timestamp:
            case Type::Kind::Duration: {
                append_bytes(stream, column.data.bytes().subspan(index * 8, 8));
                break;
            }
        }
    }
    return crc32(stream);
}

}  // namespace arrowlane
