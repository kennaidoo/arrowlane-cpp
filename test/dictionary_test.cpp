#include "harness.hpp"

#include "arrowlane/column.hpp"
#include "arrowlane/ipc.hpp"
#include "arrowlane/lanes.hpp"
#include "arrowlane/schema.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

// One field, three rows, two dictionary values: the smallest batch that proves
// the whole path.
arrowlane::RecordBatch make_dictionary_batch() {
    arrowlane::Schema schema;
    arrowlane::Field field;
    field.name = "merchant";
    field.type = arrowlane::Type::utf8();
    field.dictionary = arrowlane::DictionaryEncoding{0, 4, false};
    schema.fields.push_back(field);

    arrowlane::ColumnBuilder indices(arrowlane::Type::integer(32, true));
    indices.append_int(0);
    indices.append_int(1);
    indices.append_int(0);

    arrowlane::ColumnBuilder values(arrowlane::Type::utf8());
    const std::string first = "ozow";
    const std::string second = "payfast";
    values.append_bytes(
        {reinterpret_cast<const std::byte*>(first.data()), first.size()});
    values.append_bytes(
        {reinterpret_cast<const std::byte*>(second.data()), second.size()});

    arrowlane::RecordBatch batch;
    batch.schema = schema;
    batch.columns.push_back(indices.finish());
    batch.dictionaries.push_back(
        arrowlane::DictionaryColumn{0, arrowlane::Type::utf8(), values.finish()});
    return batch;
}

}  // namespace

AL_TEST(dictionary_batch_round_trips_through_ipc) {
    const arrowlane::RecordBatch batch = make_dictionary_batch();
    auto stream = arrowlane::encode_ipc(batch);
    AL_REQUIRE(stream.has_value());

    auto decoded = arrowlane::decode_ipc(*stream);
    AL_REQUIRE(decoded.has_value());
    AL_CHECK_EQ(decoded->rows(), std::int64_t{3});
    AL_REQUIRE(decoded->dictionaries.size() == 1);
    AL_CHECK_EQ(decoded->dictionaries[0].values.length, std::int64_t{2});
    AL_CHECK(decoded->schema.fields[0].dictionary.has_value());
    AL_CHECK_EQ(decoded->columns[0].data.size(), std::size_t{12});
}

AL_TEST(dictionary_batch_is_written_for_the_pyarrow_check) {
    const arrowlane::RecordBatch batch = make_dictionary_batch();
    auto stream = arrowlane::encode_ipc(batch);
    AL_REQUIRE(stream.has_value());

    // ctest runs in the build directory, so this lands next to the test binary
    // and works with a read only source mount in the container.
    const std::string path = "dict_cpp.arrows";
    std::ofstream out(path, std::ios::binary);
    AL_REQUIRE(out.good());
    out.write(reinterpret_cast<const char*>(stream->data()),
              static_cast<std::streamsize>(stream->size()));
    out.close();
    AL_CHECK_EQ(std::ifstream(path, std::ios::binary).good(), true);
}

AL_TEST(dictionary_stream_from_pyarrow_decodes) {
    const std::string path = std::string(AL_FIXTURE_DIR) + "/dict_pyarrow.arrows";
    std::ifstream in(path, std::ios::binary);
    AL_REQUIRE(in.good());
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(in.tellg());
    AL_REQUIRE(size > 0);
    in.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(size);
    in.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(size));
    AL_CHECK_EQ(static_cast<std::size_t>(in.gcount()), bytes.size());

    auto decoded = arrowlane::decode_ipc(bytes);
    AL_REQUIRE(decoded.has_value());
    AL_CHECK_EQ(decoded->rows(), std::int64_t{3});
    AL_REQUIRE(decoded->dictionaries.size() == 1);
    AL_CHECK(decoded->schema.fields[0].dictionary.has_value());
    AL_CHECK_EQ(decoded->dictionaries[0].values.length, std::int64_t{2});
    // The indices must survive: 0, 1, 0.
    const auto* raw = decoded->columns[0].data.bytes().data();
    AL_CHECK_EQ(static_cast<int>(static_cast<std::int8_t>(raw[0])), 0);
    AL_CHECK_EQ(static_cast<int>(static_cast<std::int8_t>(raw[4])), 1);
    AL_CHECK_EQ(static_cast<int>(static_cast<std::int8_t>(raw[8])), 0);
}

AL_TEST(dictionary_column_renders_values_in_json) {
    const arrowlane::RecordBatch batch = make_dictionary_batch();
    auto frame = arrowlane::encode_lane(arrowlane::LaneId::Json, batch);
    AL_REQUIRE(frame.has_value());
    const std::string payload(reinterpret_cast<const char*>(frame->payload.data()),
                              frame->payload.size());
    AL_CHECK(payload.find("\"merchant\":\"ozow\"") != std::string::npos);
    AL_CHECK(payload.find("\"merchant\":\"payfast\"") != std::string::npos);
    // The indices must not leak into the readable lane.
    AL_CHECK(payload.find("\"merchant\":0") == std::string::npos);
}
