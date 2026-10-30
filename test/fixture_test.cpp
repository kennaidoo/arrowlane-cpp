#include "harness.hpp"

#include "arrowlane/column.hpp"
#include "arrowlane/fixture.hpp"

#include <cmath>
#include <cstring>
#include <string>

namespace {

std::int64_t read_i64(const arrowlane::Column& column, std::int64_t row) {
    const auto bytes = column.bytes_at(row);
    std::int64_t value = 0;
    if (bytes.size() >= sizeof(value)) std::memcpy(&value, bytes.data(), sizeof(value));
    return value;
}

std::int32_t read_i32(const arrowlane::Column& column, std::int64_t row) {
    const auto bytes = column.bytes_at(row);
    std::int32_t value = 0;
    if (bytes.size() >= sizeof(value)) std::memcpy(&value, bytes.data(), sizeof(value));
    return value;
}

double read_f64(const arrowlane::Column& column, std::int64_t row) {
    const auto bytes = column.bytes_at(row);
    double value = 0.0;
    if (bytes.size() >= sizeof(value)) std::memcpy(&value, bytes.data(), sizeof(value));
    return value;
}

std::string read_text(const arrowlane::Column& column, std::int64_t row) {
    const auto bytes = column.bytes_at(row);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

}  // namespace

AL_TEST(fixture_has_the_eight_columns) {
    const auto batch = arrowlane::make_fixture(8);
    AL_CHECK_EQ(batch.rows(), std::int64_t{8});
    AL_REQUIRE(batch.columns.size() == 8);
    AL_CHECK_EQ(batch.schema.fields[0].name, std::string("id"));
    AL_CHECK_EQ(batch.schema.fields[7].name, std::string("blob"));
    AL_CHECK(arrowlane::validate_batch(batch).has_value());
}

AL_TEST(fixture_row_zero) {
    const auto batch = arrowlane::make_fixture(8);
    AL_REQUIRE(batch.columns.size() == 8);
    AL_CHECK_EQ(read_i64(batch.columns[0], 0), std::int64_t{0});
    AL_CHECK(!batch.columns[1].valid(0));
    // Booleans are bit packed, so bytes_at is empty and the bit is read from
    // the data buffer. A false value is still a valid value.
    AL_CHECK(batch.columns[3].valid(0));
    AL_REQUIRE(batch.columns[3].data.size() >= 1);
    AL_CHECK_EQ((static_cast<unsigned>(batch.columns[3].data.bytes()[0]) & 0x01u), 1u);
    AL_CHECK_EQ(read_i32(batch.columns[4], 0), std::int32_t{20454});
    AL_CHECK_EQ(read_i64(batch.columns[6], 0), std::int64_t{1793188800000000});
    AL_CHECK_EQ(batch.columns[7].bytes_at(0).size(), std::size_t{3});
}

AL_TEST(fixture_row_one) {
    const auto batch = arrowlane::make_fixture(8);
    AL_REQUIRE(batch.columns.size() == 8);
    AL_CHECK_EQ(read_i64(batch.columns[0], 1), std::int64_t{1});
    AL_CHECK(batch.columns[1].valid(1));
    AL_CHECK_EQ(read_text(batch.columns[1], 1), std::string("row-1"));
    AL_CHECK(std::fabs(read_f64(batch.columns[2], 1) - (1.0 / 3.0)) < 1e-15);
    AL_CHECK(batch.columns[3].valid(1));
    AL_CHECK_EQ((static_cast<unsigned>(batch.columns[3].data.bytes()[0]) >> 1 & 0x01u), 0u);
    AL_CHECK_EQ(read_i32(batch.columns[4], 1), std::int32_t{20455});
}

AL_TEST(fixture_row_seven_is_unnamed) {
    const auto batch = arrowlane::make_fixture(8);
    AL_REQUIRE(batch.columns.size() == 8);
    AL_CHECK_EQ(read_i64(batch.columns[0], 7), std::int64_t{7});
    AL_CHECK(!batch.columns[1].valid(7));
    AL_CHECK_EQ(read_i32(batch.columns[4], 7), std::int32_t{20461});
    const auto amount = batch.columns[5].bytes_at(7);
    AL_REQUIRE(amount.size() == 16);
    AL_CHECK_EQ(static_cast<unsigned>(amount[0]), 0xD2u);
    AL_CHECK_EQ(static_cast<unsigned>(amount[1]), 0x04u);
    AL_CHECK_EQ(static_cast<unsigned>(amount[2]), 0x00u);
}
