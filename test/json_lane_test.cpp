#include "harness.hpp"

#include "arrowlane/digest.hpp"
#include "arrowlane/fixture.hpp"
#include "arrowlane/lanes.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

namespace {

std::string payload_of(const arrowlane::Frame& frame) {
    return std::string(reinterpret_cast<const char*>(frame.payload.data()),
                       frame.payload.size());
}

void set_payload(arrowlane::Frame& frame, const std::string& text) {
    frame.payload.assign(
        reinterpret_cast<const std::byte*>(text.data()),
        reinterpret_cast<const std::byte*>(text.data()) + text.size());
    frame.uncompressed_len = frame.payload.size();
}

}  // namespace

AL_TEST(json_lane_matches_the_python_bytes) {
    const auto batch = arrowlane::make_fixture(3);
    auto frame = arrowlane::encode_lane(arrowlane::LaneId::Json, batch);
    AL_REQUIRE(frame.has_value());
    const std::string payload = payload_of(*frame);
    const std::string expected =
        R"({"id":0,"name":null,"score":0.0,"ok":true,"day":{"$":"date","v":"2026-01-01"},"amount":{"$":"dec","v":"12.34"},"ts":{"$":"ts","v":"2026-10-28T12:00:00+00:00"},"blob":{"$":"bin","v":"AAEC"}})"
        "\n"
        R"({"id":1,"name":"row-1","score":0.3333333333333333,"ok":false,"day":{"$":"date","v":"2026-01-02"},"amount":{"$":"dec","v":"12.34"},"ts":{"$":"ts","v":"2026-10-28T12:00:00+00:00"},"blob":{"$":"bin","v":"AAEC"}})"
        "\n"
        R"({"id":2,"name":"row-2","score":0.6666666666666666,"ok":true,"day":{"$":"date","v":"2026-01-03"},"amount":{"$":"dec","v":"12.34"},"ts":{"$":"ts","v":"2026-10-28T12:00:00+00:00"},"blob":{"$":"bin","v":"AAEC"}})";
    AL_CHECK_EQ(payload, expected);
    AL_CHECK_EQ(frame->rows, std::uint64_t{3});
    AL_CHECK_EQ(frame->uncompressed_len, std::uint64_t{expected.size()});
}

AL_TEST(json_lane_roundtrips_the_fixture) {
    using namespace arrowlane;
    const auto batch = make_fixture(3);
    auto frame = encode_lane(LaneId::Json, batch);
    AL_REQUIRE(frame.has_value());
    auto decoded = decode_lane(*frame);
    AL_REQUIRE(decoded.has_value());
    AL_CHECK(decoded->schema == batch.schema);
    AL_CHECK_EQ(decoded->rows(), batch.rows());
    for (std::size_t index = 0; index < batch.columns.size(); ++index) {
        AL_CHECK_EQ(column_digest(decoded->columns[index], batch.schema.fields[index].type),
                    column_digest(batch.columns[index], batch.schema.fields[index].type));
    }
}

AL_TEST(json_lane_roundtrips_every_type) {
    using namespace arrowlane;
    RecordBatch batch;
    Schema schema;
    schema.fields.push_back(Field{"n", Type::null(), true});
    schema.fields.push_back(Field{"b", Type::boolean(), true});
    schema.fields.push_back(Field{"i8", Type::integer(8, true), true});
    schema.fields.push_back(Field{"u32", Type::integer(32, false), true});
    schema.fields.push_back(Field{"f32", Type::floating(32), true});
    schema.fields.push_back(Field{"lu", Type::large_utf8(), true});
    schema.fields.push_back(Field{"lb", Type::large_binary(), true});
    schema.fields.push_back(Field{"fsb", Type::fixed_size_binary(3), true});
    schema.fields.push_back(Field{"d64", Type::date(1), true});
    schema.fields.push_back(Field{"t32", Type::time(1, 32), true});
    schema.fields.push_back(Field{"t64", Type::time(3, 64), true});
    schema.fields.push_back(Field{"ts", Type::timestamp(0), true});
    schema.fields.push_back(Field{"dur", Type::duration(1), true});
    batch.schema = schema;

    ColumnBuilder n(Type::null());
    ColumnBuilder b(Type::boolean());
    ColumnBuilder i8(Type::integer(8, true));
    ColumnBuilder u32(Type::integer(32, false));
    ColumnBuilder f32(Type::floating(32));
    ColumnBuilder lu(Type::large_utf8());
    ColumnBuilder lb(Type::large_binary());
    ColumnBuilder fsb(Type::fixed_size_binary(3));
    ColumnBuilder d64(Type::date(1));
    ColumnBuilder t32(Type::time(1, 32));
    ColumnBuilder t64(Type::time(3, 64));
    ColumnBuilder ts(Type::timestamp(0));
    ColumnBuilder dur(Type::duration(1));

    n.append_null();
    b.append_bool(true);
    i8.append_int(-5);
    u32.append_uint(4'294'967'295u);
    f32.append_double(0.5);
    const std::string text = "héllo";
    lu.append_bytes(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text.data()), text.size()));
    lb.append_bytes(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text.data()), text.size()));
    const std::array<std::byte, 3> fixed{std::byte{1}, std::byte{2}, std::byte{3}};
    fsb.append_bytes(fixed);
    d64.append_int(20454LL * 86'400'000);
    t32.append_int(45'296'789);
    t64.append_int(45'296'789'123'000LL);
    ts.append_int(1793188800LL);
    dur.append_int(1'500);

    batch.columns.push_back(n.finish());
    batch.columns.push_back(b.finish());
    batch.columns.push_back(i8.finish());
    batch.columns.push_back(u32.finish());
    batch.columns.push_back(f32.finish());
    batch.columns.push_back(lu.finish());
    batch.columns.push_back(lb.finish());
    batch.columns.push_back(fsb.finish());
    batch.columns.push_back(d64.finish());
    batch.columns.push_back(t32.finish());
    batch.columns.push_back(t64.finish());
    batch.columns.push_back(ts.finish());
    batch.columns.push_back(dur.finish());

    auto frame = encode_lane(LaneId::Json, batch);
    AL_REQUIRE(frame.has_value());
    auto decoded = decode_lane(*frame);
    AL_REQUIRE(decoded.has_value());
    AL_CHECK(decoded->schema == batch.schema);
    for (std::size_t index = 0; index < batch.columns.size(); ++index) {
        const std::uint32_t want =
            column_digest(batch.columns[index], batch.schema.fields[index].type);
        const std::uint32_t got =
            column_digest(decoded->columns[index], batch.schema.fields[index].type);
        if (got != want) {
            std::printf("digest mismatch at column %s\n",
                        batch.schema.fields[index].name.c_str());
        }
        AL_CHECK_EQ(got, want);
    }
}

AL_TEST(json_lane_rejects_bad_rows) {
    using namespace arrowlane;
    const auto batch = make_fixture(2);
    auto frame = encode_lane(LaneId::Json, batch);
    AL_REQUIRE(frame.has_value());

    Frame missing = *frame;
    std::string text = payload_of(missing);
    const std::string head = R"({"id":0,)";
    text.erase(0, head.size());
    set_payload(missing, text);
    AL_CHECK_ERR(decode_lane(missing), Code::BadHeader);

    Frame extra = *frame;
    text = payload_of(extra);
    const std::string tail = R"(,"zz":1})";
    text.insert(text.size() - 1, tail);
    set_payload(extra, text);
    AL_CHECK_ERR(decode_lane(extra), Code::BadHeader);

    Frame tag = *frame;
    text = payload_of(tag);
    const std::size_t position = text.find(R"("bin")");
    AL_REQUIRE(position != std::string::npos);
    text.replace(position, 5, R"("bogus")");
    set_payload(tag, text);
    AL_CHECK_ERR(decode_lane(tag), Code::BadHeader);

    Frame type = *frame;
    text = payload_of(type);
    const std::size_t name = text.find(R"("row-1")");
    if (name != std::string::npos) {
        text.replace(name, 7, R"(7)");
        set_payload(type, text);
        AL_CHECK_ERR(decode_lane(type), Code::BadHeader);
    }

    Frame rows = *frame;
    rows.rows = 3;
    AL_CHECK_ERR(decode_lane(rows), Code::Frame);
}
