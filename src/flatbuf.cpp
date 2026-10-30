#include "detail/flatbuf.hpp"

#include <cassert>
#include <cstring>

#include "detail/bytes.hpp"

namespace arrowlane::detail {

FlatWriter::FlatWriter(std::size_t reserve_bytes) {
    buffer_.reserve(reserve_bytes);
    buffer_.resize(4);
}

void FlatWriter::pad_to(std::size_t alignment) {
    while (buffer_.size() % alignment != 0) {
        buffer_.push_back(std::byte{0});
    }
}

FlatWriter::Table FlatWriter::begin_table(std::size_t slots) {
    Table table;
    table.slots = slots;
    table.entries.assign(slots, 0);
    pad_to(4);
    table.pos = buffer_.size();
    buffer_.resize(table.pos + 4, std::byte{0});
    table.end = buffer_.size();
    return table;
}

void FlatWriter::add_scalar(Table& table, std::size_t slot, const void* data,
                            std::size_t size, std::size_t alignment) {
    assert(slot < table.slots);
    pad_to(alignment);
    const std::size_t field_pos = buffer_.size();
    buffer_.resize(buffer_.size() + size);
    std::memcpy(buffer_.data() + field_pos, data, size);
    table.entries[slot] = static_cast<std::uint16_t>(field_pos - table.pos);
    table.end = buffer_.size();
}

void FlatWriter::add_u8(Table& table, std::size_t slot, std::uint8_t value) {
    add_scalar(table, slot, &value, 1, 1);
}

void FlatWriter::add_bool(Table& table, std::size_t slot, bool value) {
    const std::uint8_t raw = value ? 1 : 0;
    add_scalar(table, slot, &raw, 1, 1);
}

void FlatWriter::add_i16(Table& table, std::size_t slot, std::int16_t value) {
    add_scalar(table, slot, &value, 2, 2);
}

void FlatWriter::add_i32(Table& table, std::size_t slot, std::int32_t value) {
    add_scalar(table, slot, &value, 4, 4);
}

void FlatWriter::add_i64(Table& table, std::size_t slot, std::int64_t value) {
    add_scalar(table, slot, &value, 8, 8);
}

std::size_t FlatWriter::add_offset(Table& table, std::size_t slot) {
    assert(slot < table.slots);
    pad_to(4);
    const std::size_t field_pos = buffer_.size();
    buffer_.resize(buffer_.size() + 4);
    table.entries[slot] = static_cast<std::uint16_t>(field_pos - table.pos);
    table.end = buffer_.size();
    return field_pos;
}

std::size_t FlatWriter::record_vtable(std::span<const std::byte> bytes) {
    const std::string key(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const auto found = vtables_.find(key);
    if (found != vtables_.end()) {
        return found->second;
    }
    pad_to(2);
    const std::size_t position = buffer_.size();
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    vtables_.emplace(key, position);
    return position;
}

void FlatWriter::end_table(Table& table) {
    const std::size_t table_size = table.end - table.pos;
    assert(table_size <= 0xFFFF);
    std::vector<std::byte> vtable(4 + 2 * table.slots, std::byte{0});
    store_le<std::uint16_t>(vtable.data(), static_cast<std::uint16_t>(vtable.size()));
    store_le<std::uint16_t>(vtable.data() + 2, static_cast<std::uint16_t>(table_size));
    for (std::size_t slot = 0; slot < table.slots; ++slot) {
        store_le<std::uint16_t>(vtable.data() + 4 + 2 * slot, table.entries[slot]);
    }
    // The vtable may sit after the table, which makes the soffset negative.
    // The format allows this: a reader computes vtable = table - soffset.
    const std::size_t vtable_pos = record_vtable(vtable);
    store_le<std::int32_t>(
        buffer_.data() + table.pos,
        static_cast<std::int32_t>(static_cast<std::int64_t>(table.pos) -
                                  static_cast<std::int64_t>(vtable_pos)));
}

std::size_t FlatWriter::add_string(std::string_view text) {
    pad_to(4);
    const std::size_t pos = buffer_.size();
    buffer_.resize(buffer_.size() + 4);
    store_le<std::uint32_t>(buffer_.data() + pos, static_cast<std::uint32_t>(text.size()));
    const std::size_t start = buffer_.size();
    buffer_.resize(buffer_.size() + text.size() + 1);
    std::memcpy(buffer_.data() + start, text.data(), text.size());
    pad_to(4);
    return pos;
}

FlatWriter::OffsetVector FlatWriter::add_offset_vector(std::size_t count) {
    pad_to(4);
    OffsetVector vector;
    vector.pos = buffer_.size();
    vector.count = count;
    buffer_.resize(buffer_.size() + 4 + 4 * count, std::byte{0});
    store_le<std::uint32_t>(buffer_.data() + vector.pos, static_cast<std::uint32_t>(count));
    vector.data = vector.pos + 4;
    return vector;
}

void FlatWriter::set_element(OffsetVector& vector, std::size_t index, std::size_t target) {
    assert(index < vector.count);
    const std::size_t field_pos = vector.data + 4 * index;
    store_le<std::uint32_t>(buffer_.data() + field_pos,
                            static_cast<std::uint32_t>(target - field_pos));
}

FlatWriter::StructVector FlatWriter::add_struct_vector(std::size_t count,
                                                       std::size_t struct_size,
                                                       std::size_t alignment) {
    while ((buffer_.size() + 4) % alignment != 0) {
        buffer_.push_back(std::byte{0});
    }
    StructVector vector;
    vector.pos = buffer_.size();
    vector.count = count;
    buffer_.resize(buffer_.size() + 4);
    store_le<std::uint32_t>(buffer_.data() + vector.pos, static_cast<std::uint32_t>(count));
    vector.data = buffer_.size();
    buffer_.resize(buffer_.size() + count * struct_size, std::byte{0});
    return vector;
}

void FlatWriter::patch_offset(std::size_t field_pos, std::size_t target) {
    store_le<std::uint32_t>(buffer_.data() + field_pos,
                            static_cast<std::uint32_t>(target - field_pos));
}

std::vector<std::byte> FlatWriter::finish(std::size_t root) {
    pad_to(8);
    store_le<std::uint32_t>(buffer_.data(), static_cast<std::uint32_t>(root));
    return std::move(buffer_);
}

std::optional<FlatReader::Table> FlatReader::table_at(std::size_t pos) const {
    if (pos + 4 > data_.size()) return std::nullopt;
    const std::int32_t offset = load_le<std::int32_t>(data_.data() + pos);
    const std::int64_t vtable = static_cast<std::int64_t>(pos) - offset;
    if (vtable < 0 || static_cast<std::uint64_t>(vtable) + 4 > data_.size()) {
        return std::nullopt;
    }
    const auto vpos = static_cast<std::size_t>(vtable);
    const std::uint16_t vtable_bytes = load_le<std::uint16_t>(data_.data() + vpos);
    const std::uint16_t table_bytes = load_le<std::uint16_t>(data_.data() + vpos + 2);
    if (vtable_bytes < 4 || vpos + vtable_bytes > data_.size()) return std::nullopt;
    if (pos + table_bytes > data_.size()) return std::nullopt;
    return Table{pos, vpos, vtable_bytes, table_bytes};
}

std::optional<FlatReader::Table> FlatReader::root() const {
    if (data_.size() < 4) return std::nullopt;
    const std::uint32_t pos = load_le<std::uint32_t>(data_.data());
    if (pos >= data_.size()) return std::nullopt;
    return table_at(pos);
}

std::optional<std::size_t> FlatReader::field(const Table& table, std::size_t slot) const {
    const std::size_t entry = 4 + 2 * slot;
    if (entry + 2 > table.vtable_bytes) return std::nullopt;
    const std::uint16_t offset = load_le<std::uint16_t>(data_.data() + table.vtable + entry);
    if (offset == 0) return std::nullopt;
    const std::size_t position = table.pos + offset;
    if (position >= table.pos + table.table_bytes) return std::nullopt;
    return position;
}

std::optional<std::uint8_t> FlatReader::u8(const Table& table, std::size_t slot) const {
    const auto position = field(table, slot);
    if (!position || *position + 1 > data_.size()) return std::nullopt;
    return load_le<std::uint8_t>(data_.data() + *position);
}

std::optional<std::int16_t> FlatReader::i16(const Table& table, std::size_t slot) const {
    const auto position = field(table, slot);
    if (!position || *position + 2 > data_.size()) return std::nullopt;
    return load_le<std::int16_t>(data_.data() + *position);
}

std::optional<std::int32_t> FlatReader::i32(const Table& table, std::size_t slot) const {
    const auto position = field(table, slot);
    if (!position || *position + 4 > data_.size()) return std::nullopt;
    return load_le<std::int32_t>(data_.data() + *position);
}

std::optional<std::int64_t> FlatReader::i64(const Table& table, std::size_t slot) const {
    const auto position = field(table, slot);
    if (!position || *position + 8 > data_.size()) return std::nullopt;
    return load_le<std::int64_t>(data_.data() + *position);
}

std::optional<std::size_t> FlatReader::target(const Table& table, std::size_t slot) const {
    const auto position = field(table, slot);
    if (!position || *position + 4 > data_.size()) return std::nullopt;
    const std::uint32_t offset = load_le<std::uint32_t>(data_.data() + *position);
    const std::size_t result = *position + offset;
    if (result >= data_.size()) return std::nullopt;
    return result;
}

std::optional<std::string_view> FlatReader::string(const Table& table,
                                                   std::size_t slot) const {
    const auto position = target(table, slot);
    if (!position || *position + 4 > data_.size()) return std::nullopt;
    const std::uint32_t length = load_le<std::uint32_t>(data_.data() + *position);
    const std::size_t start = *position + 4;
    if (start + length > data_.size()) return std::nullopt;
    return std::string_view(reinterpret_cast<const char*>(data_.data() + start), length);
}

std::optional<FlatReader::Vector> FlatReader::vector(const Table& table,
                                                     std::size_t slot) const {
    const auto position = target(table, slot);
    if (!position || *position + 4 > data_.size()) return std::nullopt;
    const std::uint32_t count = load_le<std::uint32_t>(data_.data() + *position);
    Vector result;
    result.count = count;
    result.data = *position + 4;
    if (result.data > data_.size()) return std::nullopt;
    return result;
}

std::optional<std::size_t> FlatReader::element_target(const Vector& vector,
                                                      std::size_t index) const {
    if (index >= vector.count) return std::nullopt;
    const std::size_t field_pos = vector.data + 4 * index;
    if (field_pos + 4 > data_.size()) return std::nullopt;
    const std::uint32_t offset = load_le<std::uint32_t>(data_.data() + field_pos);
    const std::size_t result = field_pos + offset;
    if (result >= data_.size()) return std::nullopt;
    return result;
}

}  // namespace arrowlane::detail
