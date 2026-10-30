#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace arrowlane::detail {

class FlatWriter {
public:
    explicit FlatWriter(std::size_t reserve_bytes = 1024);

    struct Table {
        std::size_t pos = 0;
        std::size_t slots = 0;
        std::size_t end = 0;
        std::vector<std::uint16_t> entries;
    };

    struct OffsetVector {
        std::size_t pos = 0;
        std::size_t data = 0;
        std::size_t count = 0;
    };

    struct StructVector {
        std::size_t pos = 0;
        std::size_t data = 0;
        std::size_t count = 0;
        std::size_t stride = 0;
    };

    Table begin_table(std::size_t slots);
    void add_u8(Table& table, std::size_t slot, std::uint8_t value);
    void add_bool(Table& table, std::size_t slot, bool value);
    void add_i16(Table& table, std::size_t slot, std::int16_t value);
    void add_i32(Table& table, std::size_t slot, std::int32_t value);
    void add_i64(Table& table, std::size_t slot, std::int64_t value);
    std::size_t add_offset(Table& table, std::size_t slot);
    void end_table(Table& table);

    std::size_t add_string(std::string_view text);
    OffsetVector add_offset_vector(std::size_t count);
    void set_element(OffsetVector& vector, std::size_t index, std::size_t target);
    StructVector add_struct_vector(std::size_t count, std::size_t struct_size,
                                   std::size_t alignment);
    void write_struct(StructVector& vector, std::size_t index,
                      std::span<const std::byte> bytes);
    void patch_offset(std::size_t field, std::size_t target);
    std::vector<std::byte> finish(std::size_t root);

private:
    void pad_to(std::size_t alignment);
    void add_scalar(Table& table, std::size_t slot, const void* data, std::size_t size,
                    std::size_t alignment);
    // Returns the position of a vtable with these exact bytes, appending it
    // when no earlier table shares the shape and size.
    std::size_t record_vtable(std::span<const std::byte> bytes);

    std::vector<std::byte> buffer_;
    std::unordered_map<std::string, std::size_t> vtables_;
};

class FlatReader {
public:
    explicit FlatReader(std::span<const std::byte> data) : data_(data) {}

    struct Table {
        std::size_t pos = 0;
        std::size_t vtable = 0;
        std::uint16_t vtable_bytes = 0;
        std::uint16_t table_bytes = 0;
    };

    struct Vector {
        std::size_t count = 0;
        std::size_t data = 0;
    };

    [[nodiscard]] std::optional<Table> root() const;
    [[nodiscard]] std::optional<Table> table_at(std::size_t pos) const;
    [[nodiscard]] std::optional<std::size_t> field(const Table& table,
                                                   std::size_t slot) const;
    [[nodiscard]] std::optional<std::uint8_t> u8(const Table& table, std::size_t slot) const;
    [[nodiscard]] std::optional<std::int16_t> i16(const Table& table, std::size_t slot) const;
    [[nodiscard]] std::optional<std::int32_t> i32(const Table& table, std::size_t slot) const;
    [[nodiscard]] std::optional<std::int64_t> i64(const Table& table, std::size_t slot) const;
    [[nodiscard]] std::optional<std::size_t> target(const Table& table, std::size_t slot) const;
    [[nodiscard]] std::optional<std::string_view> string(const Table& table,
                                                         std::size_t slot) const;
    [[nodiscard]] std::optional<Vector> vector(const Table& table, std::size_t slot) const;
    [[nodiscard]] std::optional<std::size_t> element_target(const Vector& vector,
                                                             std::size_t index) const;

private:
    std::span<const std::byte> data_;
};

// Prefixes a finished flatbuffer with the Arrow encapsulation marker and length.
[[nodiscard]] std::vector<std::byte> encapsulate(std::vector<std::byte> flat);

}  // namespace arrowlane::detail
