#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "arrowlane/buffer.hpp"
#include "arrowlane/error.hpp"
#include "arrowlane/schema.hpp"

namespace arrowlane {

struct Column {
    Buffer validity;
    Buffer offsets;
    Buffer data;
    std::int64_t length = 0;
    std::int64_t null_count = 0;

    [[nodiscard]] bool valid(std::int64_t row) const noexcept;
    [[nodiscard]] std::span<const std::byte> bytes_at(std::int64_t row) const;
};

// One dictionary, carried beside the columns that index it. The values column is
// an ordinary Column of the field value type, held here so the IPC writer does
// not need a schema lookup to find it.
struct DictionaryColumn {
    std::int64_t id = 0;
    Type value_type;
    Column values;
};

struct RecordBatch {
    Schema schema;
    std::vector<Column> columns;
    // Empty unless a field is dictionary encoded.
    std::vector<DictionaryColumn> dictionaries;

    [[nodiscard]] std::int64_t rows() const noexcept;
};

[[nodiscard]] std::int64_t estimate_bytes(const RecordBatch& batch) noexcept;

[[nodiscard]] Result<void> validate_batch(const RecordBatch& batch);

class ColumnBuilder {
public:
    explicit ColumnBuilder(const Type& type);
    void append_null();
    void append_int(std::int64_t value);
    void append_uint(std::uint64_t value);
    void append_double(double value);
    void append_bool(bool value);
    void append_half(std::uint16_t bits);
    void append_bytes(std::span<const std::byte> value);
    void append_decimal(std::span<const std::byte> little_endian);
    Column finish();

private:
    void push_validity(bool valid);
    void pad_fixed();
    void append_fixed(std::uint64_t raw, std::size_t width);
    void append_offset_entry();
    void push_offset(std::uint64_t value);

    Type type_;
    std::vector<std::byte> validity_;
    std::vector<std::byte> offsets_;
    std::vector<std::byte> data_;
    std::int64_t length_ = 0;
    std::int64_t null_count_ = 0;
    std::uint64_t offset_ = 0;
};

}  // namespace arrowlane
