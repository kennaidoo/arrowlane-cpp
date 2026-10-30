#include "harness.hpp"

#include "arrowlane/digest.hpp"
#include "arrowlane/fixture.hpp"
#include "arrowlane/ipc.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "detail/bytes.hpp"

namespace {

arrowlane::RecordBatch make_two_column_batch() {
    using namespace arrowlane;
    RecordBatch batch;
    batch.schema.fields.push_back(Field{"id", Type::integer(64, true), true});
    batch.schema.fields.push_back(Field{"name", Type::utf8(), true});

    ColumnBuilder id(Type::integer(64, true));
    id.append_int(1);
    id.append_int(2);
    ColumnBuilder name(Type::utf8());
    name.append_null();
    const std::string text = "a";
    name.append_bytes(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text.data()), text.size()));

    batch.columns.push_back(id.finish());
    batch.columns.push_back(name.finish());
    return batch;
}

std::vector<std::byte> read_fixture(const char* name) {
    const std::string path = std::string(AL_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    stream.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(stream.tellg());
    stream.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(size);
    if (size > 0) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(size));
    }
    return bytes;
}

void check_matches_fixture(const arrowlane::RecordBatch& decoded,
                           const arrowlane::RecordBatch& expected) {
    AL_CHECK(decoded.schema == expected.schema);
    AL_CHECK_EQ(decoded.rows(), expected.rows());
    for (std::size_t index = 0; index < expected.columns.size(); ++index) {
        const auto want = arrowlane::column_digest(expected.columns[index],
                                                   expected.schema.fields[index].type);
        const auto got = arrowlane::column_digest(decoded.columns[index],
                                                  decoded.schema.fields[index].type);
        if (got != want) {
            std::printf("digest mismatch at column %s\n",
                        expected.schema.fields[index].name.c_str());
        }
        AL_CHECK_EQ(got, want);
    }
}

}  // namespace

AL_TEST(ipc_body_layout) {
    const auto batch = make_two_column_batch();
    auto layout = arrowlane::plan_ipc_body(batch);
    AL_REQUIRE(layout.has_value());
    AL_CHECK_EQ(layout->nodes.size(), std::size_t{2});
    AL_CHECK_EQ(layout->nodes[0].first, std::int64_t{2});
    AL_CHECK_EQ(layout->nodes[0].second, std::int64_t{0});
    AL_CHECK_EQ(layout->nodes[1].second, std::int64_t{1});
    AL_REQUIRE(layout->buffers.size() == 5);
    AL_CHECK_EQ(layout->buffers[0].offset, std::uint64_t{0});
    AL_CHECK_EQ(layout->buffers[0].length, std::uint64_t{0});
    AL_CHECK_EQ(layout->buffers[1].offset, std::uint64_t{0});
    AL_CHECK_EQ(layout->buffers[1].length, std::uint64_t{16});
    AL_CHECK_EQ(layout->buffers[2].offset, std::uint64_t{16});
    AL_CHECK_EQ(layout->buffers[2].length, std::uint64_t{1});
    AL_CHECK_EQ(layout->buffers[3].offset, std::uint64_t{24});
    AL_CHECK_EQ(layout->buffers[3].length, std::uint64_t{12});
    AL_CHECK_EQ(layout->buffers[4].offset, std::uint64_t{40});
    AL_CHECK_EQ(layout->buffers[4].length, std::uint64_t{1});
    AL_CHECK_EQ(layout->body_size, std::uint64_t{48});

    auto body = arrowlane::build_ipc_body(batch, *layout);
    AL_REQUIRE(body.has_value());
    AL_CHECK_EQ(body->size(), std::size_t{48});
    AL_CHECK_EQ(arrowlane::detail::load_le<std::int64_t>(body->data()), std::int64_t{1});
    AL_CHECK_EQ((*body)[16], std::byte{0x02});
    AL_CHECK_EQ(arrowlane::detail::load_le<std::uint32_t>(body->data() + 24),
                std::uint32_t{0});
    AL_CHECK_EQ(arrowlane::detail::load_le<std::uint32_t>(body->data() + 28),
                std::uint32_t{0});
    AL_CHECK_EQ(arrowlane::detail::load_le<std::uint32_t>(body->data() + 32),
                std::uint32_t{1});
    AL_CHECK_EQ((*body)[40], std::byte{'a'});
}

AL_TEST(ipc_stream_layout) {
    const auto batch = make_two_column_batch();
    auto stream = arrowlane::encode_ipc(batch);
    AL_REQUIRE(stream.has_value());
    AL_CHECK_EQ(arrowlane::detail::load_le<std::uint32_t>(stream->data()), 0xFFFFFFFFU);
    const auto schema_len =
        arrowlane::detail::load_le<std::uint32_t>(stream->data() + 4);
    AL_CHECK_EQ(schema_len % 8, std::uint32_t{0});
    const std::size_t batch_start = 8 + schema_len;
    AL_CHECK_EQ(
        arrowlane::detail::load_le<std::uint32_t>(stream->data() + batch_start),
        0xFFFFFFFFU);
    const std::size_t total = stream->size();
    AL_CHECK_EQ(arrowlane::detail::load_le<std::uint32_t>(stream->data() + total - 8),
                0xFFFFFFFFU);
    AL_CHECK_EQ(arrowlane::detail::load_le<std::uint32_t>(stream->data() + total - 4),
                std::uint32_t{0});
}

AL_TEST(ipc_roundtrip) {
    const auto batch = arrowlane::make_fixture(3);
    auto stream = arrowlane::encode_ipc(batch);
    AL_REQUIRE(stream.has_value());
    auto decoded = arrowlane::decode_ipc(*stream);
    if (!decoded) {
        std::printf("decode error: %s\n", decoded.error().to_string().c_str());
    }
    AL_REQUIRE(decoded.has_value());
    check_matches_fixture(*decoded, batch);
}

AL_TEST(ipc_reads_a_pyarrow_stream) {
    const auto bytes = read_fixture("pyarrow_3row.aln");
    AL_REQUIRE(!bytes.empty());
    auto decoded = arrowlane::decode_ipc(bytes);
    if (!decoded) {
        std::printf("decode error: %s\n", decoded.error().to_string().c_str());
    }
    AL_REQUIRE(decoded.has_value());
    check_matches_fixture(*decoded, arrowlane::make_fixture(3));
}

AL_TEST(ipc_reads_a_pyarrow_two_batch_stream) {
    const auto bytes = read_fixture("pyarrow_3row_2batch.aln");
    AL_REQUIRE(!bytes.empty());
    auto decoded = arrowlane::decode_ipc(bytes);
    if (!decoded) {
        std::printf("decode error: %s\n", decoded.error().to_string().c_str());
    }
    AL_REQUIRE(decoded.has_value());
    check_matches_fixture(*decoded, arrowlane::make_fixture(3));
}

AL_TEST(ipc_rejects_a_truncated_stream) {
    const auto batch = arrowlane::make_fixture(3);
    auto stream = arrowlane::encode_ipc(batch);
    AL_REQUIRE(stream.has_value());
    const std::vector<std::byte> cut(stream->begin(), stream->end() - 9);
    AL_CHECK_ERR(arrowlane::decode_ipc(cut), arrowlane::Code::TruncatedFrame);
}
