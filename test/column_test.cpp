#include "harness.hpp"

#include "arrowlane/column.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "detail/bytes.hpp"
#include "detail/typed_view.hpp"

namespace {

const std::array<std::byte, 3> kBlob{std::byte{0}, std::byte{1}, std::byte{2}};

arrowlane::Schema fixture_schema() {
    using namespace arrowlane;
    Schema schema;
    schema.fields.push_back(Field{"id", Type::integer(64, true), true});
    schema.fields.push_back(Field{"name", Type::utf8(), true});
    schema.fields.push_back(Field{"score", Type::floating(64), true});
    schema.fields.push_back(Field{"ok", Type::boolean(), true});
    schema.fields.push_back(Field{"day", Type::date(0), true});
    schema.fields.push_back(Field{"amount", Type::decimal(9, 2, 128), true});
    schema.fields.push_back(Field{"ts", Type::timestamp(2, "UTC"), true});
    schema.fields.push_back(Field{"blob", Type::binary(), true});
    return schema;
}

arrowlane::RecordBatch build_fixture(std::int64_t rows) {
    using namespace arrowlane;
    RecordBatch batch;
    batch.schema = fixture_schema();
    ColumnBuilder id(Type::integer(64, true));
    ColumnBuilder name(Type::utf8());
    ColumnBuilder score(Type::floating(64));
    ColumnBuilder ok(Type::boolean());
    ColumnBuilder day(Type::date(0));
    ColumnBuilder amount(Type::decimal(9, 2, 128));
    ColumnBuilder ts(Type::timestamp(2, "UTC"));
    ColumnBuilder blob(Type::binary());
    for (std::int64_t index = 0; index < rows; ++index) {
        id.append_int(index);
        if (index % 7 == 0) {
            name.append_null();
        } else {
            const std::string text = "row-" + std::to_string(index);
            name.append_bytes(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(text.data()), text.size()));
        }
        score.append_double(static_cast<double>(index) / 3.0);
        ok.append_bool(index % 2 == 0);
        day.append_int(20454 + (index % 28));
        std::array<std::byte, 16> unscaled{};
        detail::store_le<std::uint64_t>(unscaled.data(), 1234);
        amount.append_decimal(unscaled);
        ts.append_int(1793188800000000);
        blob.append_bytes(kBlob);
    }
    batch.columns.push_back(id.finish());
    batch.columns.push_back(name.finish());
    batch.columns.push_back(score.finish());
    batch.columns.push_back(ok.finish());
    batch.columns.push_back(day.finish());
    batch.columns.push_back(amount.finish());
    batch.columns.push_back(ts.finish());
    batch.columns.push_back(blob.finish());
    return batch;
}

}  // namespace

AL_TEST(column_fixed_width_values) {
    using namespace arrowlane;
    ColumnBuilder builder(Type::integer(64, true));
    builder.append_int(7);
    builder.append_null();
    builder.append_int(9);
    Column column = builder.finish();
    AL_CHECK_EQ(column.length, std::int64_t{3});
    AL_CHECK_EQ(column.null_count, std::int64_t{1});
    AL_CHECK_EQ(column.validity.size(), std::size_t{1});
    AL_CHECK(column.valid(0));
    AL_CHECK(!column.valid(1));
    AL_CHECK(column.valid(2));
    AL_CHECK_EQ(column.data.size(), std::size_t{24});
    AL_CHECK_EQ(detail::load_le<std::int64_t>(column.data.bytes().data()), std::int64_t{7});
    AL_CHECK_EQ(detail::load_le<std::int64_t>(column.data.bytes().data() + 16),
                std::int64_t{9});
}

AL_TEST(column_bool_bits) {
    using namespace arrowlane;
    ColumnBuilder builder(Type::boolean());
    builder.append_bool(true);
    builder.append_bool(false);
    builder.append_bool(true);
    builder.append_null();
    Column column = builder.finish();
    AL_CHECK_EQ(column.data.size(), std::size_t{1});
    AL_CHECK_EQ(static_cast<unsigned>(column.data[0]), 0x05u);
    AL_CHECK_EQ(column.null_count, std::int64_t{1});
    AL_CHECK(!column.valid(3));
}

AL_TEST(column_offsets_and_bytes) {
    using namespace arrowlane;
    ColumnBuilder builder(Type::utf8());
    builder.append_null();
    const std::string one = "row-1";
    builder.append_bytes(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(one.data()), one.size()));
    Column column = builder.finish();
    AL_CHECK_EQ(column.offsets.size(), std::size_t{12});
    AL_CHECK_EQ(column.data.size(), std::size_t{5});
    AL_CHECK(column.bytes_at(0).empty());
    const auto bytes = column.bytes_at(1);
    AL_CHECK_EQ(bytes.size(), std::size_t{5});
    AL_CHECK_EQ(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
                std::string("row-1"));
}

AL_TEST(column_estimate_bytes_matches_pyarrow) {
    const auto batch = build_fixture(3);
    AL_CHECK(arrowlane::validate_batch(batch).has_value());
    AL_CHECK_EQ(arrowlane::estimate_bytes(batch), std::int64_t{177});
}

AL_TEST(column_typed_views) {
    using namespace arrowlane;
    const auto batch = build_fixture(3);
    const Column& score = batch.columns[2];
    const auto doubles = detail::view_as<double>(score.data.bytes());
#if defined(__cpp_lib_start_lifetime_as) && __cpp_lib_start_lifetime_as >= 202207L
    AL_REQUIRE(doubles.size() == 3);
    AL_CHECK_EQ(doubles[0], 0.0);
    AL_CHECK(doubles[1] == 1.0 / 3.0);
    AL_CHECK(doubles[2] == 2.0 / 3.0);
#else
    // No start_lifetime_as: the documented contract is an empty span and the
    // caller uses read_at. The portable path below is checked either way.
    AL_CHECK(doubles.empty());
#endif
    AL_CHECK_EQ(detail::read_at<double>(score.data.bytes(), 1), 1.0 / 3.0);
    AL_CHECK(detail::view_as<double>(score.data.bytes().subspan(1)).empty());
}
