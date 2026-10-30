#include "arrowlane/fixture.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <span>

#include "detail/bytes.hpp"

namespace arrowlane {
namespace {

const std::array<std::byte, 3> kBlob{std::byte{0}, std::byte{1}, std::byte{2}};

}  // namespace

RecordBatch make_fixture(std::int64_t rows) {
    RecordBatch batch;
    Schema schema;
    schema.fields.push_back(Field{"id", Type::integer(64, true), true});
    schema.fields.push_back(Field{"name", Type::utf8(), true});
    schema.fields.push_back(Field{"score", Type::floating(64), true});
    schema.fields.push_back(Field{"ok", Type::boolean(), true});
    schema.fields.push_back(Field{"day", Type::date(0), true});
    schema.fields.push_back(Field{"amount", Type::decimal(9, 2, 128), true});
    schema.fields.push_back(Field{"ts", Type::timestamp(2, "UTC"), true});
    schema.fields.push_back(Field{"blob", Type::binary(), true});
    batch.schema = std::move(schema);

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

}  // namespace arrowlane
