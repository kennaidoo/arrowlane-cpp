#include "arrowlane/column.hpp"

#include <utility>

#include "detail/bytes.hpp"

namespace arrowlane {
namespace {

bool bit_set(const Buffer& buffer, std::size_t index) noexcept {
    const std::byte byte = buffer[index / 8];
    return (byte & (std::byte{1} << (index % 8))) != std::byte{0};
}

void set_bit(std::vector<std::byte>& bytes, std::size_t index) {
    const std::size_t byte_index = index / 8;
    if (byte_index >= bytes.size()) {
        bytes.resize(byte_index + 1, std::byte{0});
    }
    bytes[byte_index] |= std::byte{1} << (index % 8);
}

void clear_bit(std::vector<std::byte>& bytes, std::size_t index) {
    const std::size_t byte_index = index / 8;
    if (byte_index >= bytes.size()) {
        bytes.resize(byte_index + 1, std::byte{0});
    }
    bytes[byte_index] &= ~(std::byte{1} << (index % 8));
}

}  // namespace

bool Column::valid(std::int64_t row) const noexcept {
    if (row < 0 || row >= length) return false;
    if (validity.empty()) return true;
    return bit_set(validity, static_cast<std::size_t>(row));
}

std::span<const std::byte> Column::bytes_at(std::int64_t row) const {
    if (row < 0 || row >= length) return {};
    const std::size_t index = static_cast<std::size_t>(row);
    if (!offsets.empty()) {
        const std::size_t entries = static_cast<std::size_t>(length) + 1;
        const std::size_t width = offsets.size() / entries;
        std::uint64_t start = 0;
        std::uint64_t end = 0;
        if (width == 4) {
            start = detail::load_le<std::uint32_t>(offsets.bytes().data() + index * 4);
            end = detail::load_le<std::uint32_t>(offsets.bytes().data() + (index + 1) * 4);
        } else if (width == 8) {
            start = detail::load_le<std::uint64_t>(offsets.bytes().data() + index * 8);
            end = detail::load_le<std::uint64_t>(offsets.bytes().data() + (index + 1) * 8);
        } else {
            return {};
        }
        if (end < start || end > data.size()) return {};
        return data.bytes().subspan(static_cast<std::size_t>(start),
                                    static_cast<std::size_t>(end - start));
    }
    if (length > 0 && data.size() % static_cast<std::size_t>(length) == 0) {
        const std::size_t width = data.size() / static_cast<std::size_t>(length);
        return data.bytes().subspan(index * width, width);
    }
    return {};
}

std::int64_t RecordBatch::rows() const noexcept {
    return columns.empty() ? 0 : columns.front().length;
}

std::int64_t estimate_bytes(const RecordBatch& batch) noexcept {
    std::int64_t total = 0;
    const std::int64_t rows = batch.rows();
    for (std::size_t index = 0; index < batch.columns.size(); ++index) {
        const Column& column = batch.columns[index];
        const Type& type = batch.schema.fields[index].type;
        if (type.kind == Type::Kind::Null) continue;
        if (type.is_variable_length()) {
            total += rows * type.offset_width() + static_cast<std::int64_t>(column.data.size());
        } else if (type.kind == Type::Kind::Bool) {
            total += (rows + 7) / 8;
        } else {
            total += rows * static_cast<std::int64_t>(type.width());
        }
        if (column.null_count > 0) {
            total += (rows + 7) / 8;
        }
    }
    return total;
}

Result<void> validate_batch(const RecordBatch& batch) {
    if (batch.schema.fields.size() != batch.columns.size()) {
        return std::unexpected(make_error(Code::Invalid,
                                          "schema and column counts differ"));
    }
    const std::int64_t rows = batch.columns.empty() ? 0 : batch.columns.front().length;
    for (std::size_t index = 0; index < batch.columns.size(); ++index) {
        const Column& column = batch.columns[index];
        const Type& type = batch.schema.fields[index].type;
        const std::string& name = batch.schema.fields[index].name;
        if (column.length != rows) {
            return std::unexpected(make_error(
                Code::Invalid, "column " + name + " has a different row count"));
        }
        if (column.null_count < 0 || column.null_count > column.length) {
            return std::unexpected(make_error(Code::Invalid, "column " + name +
                                                                 " has an invalid null count"));
        }
        if (column.null_count == 0 && !column.validity.empty()) {
            return std::unexpected(make_error(
                Code::Invalid, "column " + name + " carries validity with no nulls"));
        }
        if (column.null_count > 0 &&
            column.validity.size() < static_cast<std::size_t>((column.length + 7) / 8)) {
            return std::unexpected(make_error(
                Code::Invalid, "column " + name + " validity is too short"));
        }
        if (type.kind == Type::Kind::Null) {
            if (!column.data.empty()) {
                return std::unexpected(make_error(
                    Code::Invalid, "null column " + name + " carries data"));
            }
            continue;
        }
        if (batch.schema.fields[index].dictionary) {
            const DictionaryEncoding& encoding = *batch.schema.fields[index].dictionary;
            if (encoding.index_width != 4) {
                return std::unexpected(make_error(
                    Code::Invalid, "column " + name + " has a dictionary index width "
                                       "other than 4"));
            }
            if (encoding.ordered) {
                return std::unexpected(make_error(
                    Code::Invalid, "column " + name + " is an ordered dictionary"));
            }
            if (type.kind != Type::Kind::Utf8 && type.kind != Type::Kind::Binary) {
                return std::unexpected(make_error(
                    Code::Unsupported,
                    "column " + name + " is a dictionary of " + type.name()));
            }
            if (!column.offsets.empty()) {
                return std::unexpected(make_error(
                    Code::Invalid, "column " + name + " carries offsets for an index column"));
            }
            const std::size_t index_bytes =
                static_cast<std::size_t>(column.length) * encoding.index_width;
            if (column.data.size() != index_bytes) {
                return std::unexpected(make_error(
                    Code::Invalid, "column " + name + " has the wrong index bytes"));
            }
            bool found = false;
            for (const DictionaryColumn& dictionary : batch.dictionaries) {
                if (dictionary.id != encoding.id) continue;
                found = true;
                if (!(dictionary.value_type == type)) {
                    return std::unexpected(make_error(
                        Code::Invalid, "column " + name + " has a dictionary of another type"));
                }
            }
            if (!found) {
                return std::unexpected(make_error(
                    Code::Invalid, "column " + name + " has no dictionary with that id"));
            }
            continue;
        }
        if (type.is_variable_length()) {
            const std::size_t entries = static_cast<std::size_t>(column.length) + 1;
            const std::size_t width = type.offset_width();
            if (column.offsets.size() != entries * width) {
                return std::unexpected(make_error(
                    Code::Invalid, "column " + name + " has the wrong offset count"));
            }
            std::uint64_t previous = 0;
            for (std::size_t entry = 0; entry < entries; ++entry) {
                const std::uint64_t current =
                    width == 4
                        ? detail::load_le<std::uint32_t>(column.offsets.bytes().data() +
                                                         entry * 4)
                        : detail::load_le<std::uint64_t>(column.offsets.bytes().data() +
                                                         entry * 8);
                if (current < previous) {
                    return std::unexpected(make_error(
                        Code::Invalid, "column " + name + " offsets are not monotonic"));
                }
                previous = current;
            }
            if (previous != column.data.size()) {
                return std::unexpected(make_error(
                    Code::Invalid, "column " + name + " last offset does not match data"));
            }
            continue;
        }
        if (type.kind == Type::Kind::Bool) {
            const std::size_t expected =
                static_cast<std::size_t>((column.length + 7) / 8);
            if (column.data.size() != expected) {
                return std::unexpected(make_error(
                    Code::Invalid, "column " + name + " has the wrong bit count"));
            }
            continue;
        }
        const std::size_t expected =
            static_cast<std::size_t>(column.length) * type.width();
        if (column.data.size() != expected) {
            return std::unexpected(make_error(
                Code::Invalid, "column " + name + " has the wrong value bytes"));
        }
    }
    return {};
}

ColumnBuilder::ColumnBuilder(const Type& type) : type_(type) {}

void ColumnBuilder::push_validity(bool valid) {
    const std::size_t index = static_cast<std::size_t>(length_);
    if (valid) {
        set_bit(validity_, index);
    } else {
        clear_bit(validity_, index);
    }
}

void ColumnBuilder::append_fixed(std::uint64_t raw, std::size_t width) {
    const std::size_t start = data_.size();
    data_.resize(start + width, std::byte{0});
    for (std::size_t index = 0; index < width; ++index) {
        data_[start + index] = static_cast<std::byte>((raw >> (8 * index)) & 0xFF);
    }
}

void ColumnBuilder::pad_fixed() {
    if (type_.kind == Type::Kind::Bool) {
        clear_bit(data_, static_cast<std::size_t>(length_));
    } else if (type_.kind != Type::Kind::Null) {
        append_fixed(0, type_.width());
    }
}

void ColumnBuilder::push_offset(std::uint64_t value) {
    if (type_.has_offsets_64()) {
        detail::store_le<std::uint64_t>(
            offsets_.data() + offsets_.size() - 8, value);
    } else {
        detail::store_le<std::uint32_t>(
            offsets_.data() + offsets_.size() - 4, static_cast<std::uint32_t>(value));
    }
}

void ColumnBuilder::append_offset_entry() {
    offsets_.resize(offsets_.size() + (type_.has_offsets_64() ? 8 : 4), std::byte{0});
    push_offset(offset_);
}

void ColumnBuilder::append_null() {
    push_validity(false);
    ++null_count_;
    if (type_.is_variable_length()) {
        append_offset_entry();
    } else {
        pad_fixed();
    }
    ++length_;
}

void ColumnBuilder::append_int(std::int64_t value) {
    push_validity(true);
    const std::size_t width = type_.width();
    append_fixed(static_cast<std::uint64_t>(value), width);
    ++length_;
}

void ColumnBuilder::append_uint(std::uint64_t value) {
    push_validity(true);
    append_fixed(value, type_.width());
    ++length_;
}

void ColumnBuilder::append_double(double value) {
    push_validity(true);
    if (type_.float_bits == 32) {
        const float narrowed = static_cast<float>(value);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &narrowed, 4);
        append_fixed(bits, 4);
    } else {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, 8);
        append_fixed(bits, 8);
    }
    ++length_;
}

void ColumnBuilder::append_bool(bool value) {
    push_validity(true);
    if (value) {
        set_bit(data_, static_cast<std::size_t>(length_));
    } else {
        clear_bit(data_, static_cast<std::size_t>(length_));
    }
    ++length_;
}

void ColumnBuilder::append_half(std::uint16_t bits) {
    push_validity(true);
    append_fixed(bits, 2);
    ++length_;
}

void ColumnBuilder::append_bytes(std::span<const std::byte> value) {
    push_validity(true);
    if (type_.is_variable_length()) {
        append_offset_entry();
        data_.insert(data_.end(), value.begin(), value.end());
        offset_ += value.size();
    } else {
        const std::size_t width = type_.width();
        const std::size_t copy = value.size() < width ? value.size() : width;
        const std::size_t start = data_.size();
        data_.resize(start + width, std::byte{0});
        std::memcpy(data_.data() + start, value.data(), copy);
    }
    ++length_;
}

void ColumnBuilder::append_decimal(std::span<const std::byte> little_endian) {
    push_validity(true);
    const std::size_t width = type_.width();
    const std::size_t copy =
        little_endian.size() < width ? little_endian.size() : width;
    const std::size_t start = data_.size();
    data_.resize(start + width, std::byte{0});
    std::memcpy(data_.data() + start, little_endian.data(), copy);
    ++length_;
}

Column ColumnBuilder::finish() {
    if (type_.is_variable_length()) {
        append_offset_entry();
    }
    Column column;
    column.length = length_;
    column.null_count = null_count_;
    column.validity = null_count_ == 0
                          ? Buffer()
                          : Buffer(std::move(validity_));
    column.offsets = Buffer(std::move(offsets_));
    column.data = Buffer(std::move(data_));
    return column;
}

}  // namespace arrowlane
