#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "arrowlane/chunks.hpp"
#include "arrowlane/costmodel.hpp"
#include "arrowlane/digest.hpp"
#include "arrowlane/fixture.hpp"
#include "arrowlane/frame.hpp"
#include "arrowlane/ipc.hpp"
#include "arrowlane/lanes.hpp"
#include "arrowlane/schema.hpp"
#include "arrowlane/select.hpp"
#include "arrowlane/version.hpp"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <psapi.h>
#endif

namespace {

arrowlane::Schema one_schema() {
    arrowlane::Schema schema;
    schema.fields.push_back(arrowlane::Field{"a", arrowlane::Type::integer(64, true), false, std::nullopt});
    return schema;
}

arrowlane::Schema fixture_schema() {
    arrowlane::Schema schema;
    schema.fields.push_back(arrowlane::Field{"id", arrowlane::Type::integer(64, true), true, std::nullopt});
    schema.fields.push_back(arrowlane::Field{"name", arrowlane::Type::utf8(), true, std::nullopt});
    schema.fields.push_back(arrowlane::Field{"score", arrowlane::Type::floating(64), true, std::nullopt});
    schema.fields.push_back(arrowlane::Field{"ok", arrowlane::Type::boolean(), true, std::nullopt});
    schema.fields.push_back(arrowlane::Field{"day", arrowlane::Type::date(0), true, std::nullopt});
    schema.fields.push_back(
        arrowlane::Field{"amount", arrowlane::Type::decimal(9, 2, 128), true, std::nullopt});
    schema.fields.push_back(
        arrowlane::Field{"ts", arrowlane::Type::timestamp(2, "UTC"), true, std::nullopt});
    schema.fields.push_back(arrowlane::Field{"blob", arrowlane::Type::binary(), true, std::nullopt});
    return schema;
}

arrowlane::Schema types_schema() {
    arrowlane::Schema schema;
    schema.fields.push_back(arrowlane::Field{"n", arrowlane::Type::null(), true, std::nullopt});
    schema.fields.push_back(arrowlane::Field{"lb", arrowlane::Type::large_binary(), false, std::nullopt});
    schema.fields.push_back(arrowlane::Field{"lu", arrowlane::Type::large_utf8(), false, std::nullopt});
    schema.fields.push_back(arrowlane::Field{"u32", arrowlane::Type::integer(32, false), false, std::nullopt});
    schema.fields.push_back(arrowlane::Field{"f16", arrowlane::Type::floating(16), false, std::nullopt});
    schema.fields.push_back(arrowlane::Field{"d64", arrowlane::Type::date(1), false, std::nullopt});
    schema.fields.push_back(arrowlane::Field{"t32", arrowlane::Type::time(1, 32), false, std::nullopt});
    schema.fields.push_back(arrowlane::Field{"t64", arrowlane::Type::time(3, 64), false, std::nullopt});
    schema.fields.push_back(arrowlane::Field{"ts", arrowlane::Type::timestamp(0), false, std::nullopt});
    schema.fields.push_back(arrowlane::Field{"dur", arrowlane::Type::duration(1), false, std::nullopt});
    return schema;
}

int emit(const arrowlane::Schema& schema) {
    auto message = arrowlane::encode_schema_message(schema);
    if (!message) {
        std::fprintf(stderr, "error: %s\n", message.error().to_string().c_str());
        return 1;
    }
#if defined(_WIN32)
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (std::fwrite(message->data(), 1, message->size(), stdout) != message->size()) {
        std::fprintf(stderr, "error: IoError: short write\n");
        return 1;
    }
    return 0;
}

int read_message(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        std::fprintf(stderr, "error: IoError: cannot open %s\n", path);
        return 1;
    }
    stream.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(stream.tellg());
    stream.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(size);
    if (size > 0) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(size));
    }
    auto schema = arrowlane::decode_schema_message(bytes);
    if (!schema) {
        std::fprintf(stderr, "error: %s\n", schema.error().to_string().c_str());
        return 1;
    }
    std::printf("ok %zu columns\n", schema->fields.size());
    return 0;
}

int usage() {
    std::fprintf(stderr,
                 "arrowlane-cpp %s\n"
                 "\n"
                 "usage:\n"
                 "  arrowlane-cpp --version\n"
                 "  arrowlane-cpp select --rows N --bytes N [--topology T] [--policy P]\n"
                 "                          [--allow a,b] [--shm-ready] [--model FILE]\n"
                 "                          [--json]\n"
                 "  arrowlane-cpp describe [--compact]\n"
                 "  arrowlane-cpp bench [--rows N] [--repeats N] [--json]\n"
                 "  arrowlane-cpp pack --lane L [--rows N | --in FILE | --dictionary] --out FILE\n"
                 "                          [--chunks] [--rows-per-chunk N]\n"
                 "  arrowlane-cpp unpack FILE [--json | --chunks]\n"
                 "  arrowlane-cpp debug-schema one|fixture|types\n"
                 "  arrowlane-cpp debug-schema read FILE\n",
                 arrowlane::kVersion);
    return 2;
}

int fail(const arrowlane::Error& error) {
    std::fprintf(stderr, "error: %s: %s\n", arrowlane::code_name(error.code),
                 error.message.c_str());
    return 1;
}

std::vector<std::byte> read_file(const char* path) {
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

int pack_command(const char* lane_text, std::int64_t rows, const char* out_path,
                 const char* in_path, bool chunks, std::int64_t rows_per_chunk,
                 bool dictionary) {
    const auto lane = arrowlane::lane_id_from_name(lane_text);
    if (!lane) {
        std::fprintf(stderr, "error: UnknownLane: no lane named %s\n", lane_text);
        return 1;
    }
    arrowlane::RecordBatch batch;
    if (in_path != nullptr) {
        const auto stream = read_file(in_path);
        if (stream.empty()) {
            std::fprintf(stderr, "error: IoError: cannot read %s\n", in_path);
            return 1;
        }
        auto decoded = arrowlane::decode_ipc(stream);
        if (!decoded) return fail(decoded.error());
        batch = std::move(*decoded);
    } else if (dictionary) {
        batch = arrowlane::make_dictionary_fixture();
    } else {
        batch = arrowlane::make_fixture(rows);
    }
    if (chunks) {
        auto valid = arrowlane::validate_chunk_options(
            static_cast<std::size_t>(rows_per_chunk), arrowlane::kMaxPayloadBytes);
        if (!valid) return fail(valid.error());
        std::ofstream chunk_stream(out_path, std::ios::binary);
        if (!chunk_stream) {
            std::fprintf(stderr, "error: IoError: cannot open %s\n", out_path);
            return 1;
        }
        arrowlane::ChunkProducer producer(std::move(batch), *lane,
                                          static_cast<std::size_t>(rows_per_chunk),
                                          arrowlane::kMaxPayloadBytes);
        std::vector<std::byte> chunk_wire;
        while (producer.next(chunk_wire)) {
            chunk_stream.write(reinterpret_cast<const char*>(chunk_wire.data()),
                               static_cast<std::streamsize>(chunk_wire.size()));
        }
        if (producer.failed()) {
            return fail(arrowlane::make_error(producer.failure_code(),
                                             producer.failure_message()));
        }
        return 0;
    }
    auto frame = arrowlane::encode_lane(*lane, batch);
    if (!frame) return fail(frame.error());
    auto wire = arrowlane::encode_frame(std::move(*frame));
    if (!wire) return fail(wire.error());
    std::ofstream stream(out_path, std::ios::binary);
    if (!stream) {
        std::fprintf(stderr, "error: IoError: cannot open %s\n", out_path);
        return 1;
    }
    stream.write(reinterpret_cast<const char*>(wire->data()),
                 static_cast<std::streamsize>(wire->size()));
    return 0;
}

double round4(double value) { return std::round(value * 10000.0) / 10000.0; }

// Median, matching statistics.median in the reference driver. With an even
// count it averages the two middle samples.
double median_of(std::vector<double> samples) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const std::size_t middle = samples.size() / 2;
    if (samples.size() % 2 == 1) return samples[middle];
    return (samples[middle - 1] + samples[middle]) / 2.0;
}

std::vector<std::string> split_allowed(const char* text) {
    std::vector<std::string> names;
    if (text == nullptr) return names;
    std::string current;
    for (const char* cursor = text;; ++cursor) {
        const bool end = *cursor == '\0';
        if (end || *cursor == ',') {
            const auto first = current.find_first_not_of(" \t");
            if (first != std::string::npos) {
                const auto last = current.find_last_not_of(" \t");
                names.push_back(current.substr(first, last - first + 1));
            }
            current.clear();
            if (end) break;
            continue;
        }
        current.push_back(*cursor);
    }
    return names;
}

int select_command(std::int64_t rows, std::int64_t bytes, const char* topology_text,
                   const char* policy_text, const char* allow_text, bool shm_ready,
                   const char* model_path, bool as_json) {
    arrowlane::Topology topology = arrowlane::Topology::SameNetwork;
    if (topology_text != nullptr && !arrowlane::parse_topology(topology_text, topology)) {
        std::fprintf(stderr, "error: Config: unknown topology %s\n", topology_text);
        return 2;
    }
    arrowlane::Policy policy = arrowlane::Policy::Latency;
    if (policy_text != nullptr && !arrowlane::parse_policy(policy_text, policy)) {
        std::fprintf(stderr, "error: Config: unknown policy %s\n", policy_text);
        return 2;
    }
    arrowlane::Result<arrowlane::CostModel> model =
        model_path != nullptr ? arrowlane::CostModel::from_file(model_path)
                              : arrowlane::CostModel::builtin();
    if (!model) return fail(model.error());

    arrowlane::Workload workload;
    workload.rows = rows;
    workload.arrow_bytes = bytes;
    auto decision = arrowlane::select_plan(workload, topology, policy, *model,
                                           split_allowed(allow_text), shm_ready);
    if (!decision) return fail(decision.error());

    if (!as_json) {
        std::printf("lane=%s reason=%s predicted_ms=%.3f predicted_bytes=%lld\n",
                    decision->lane.c_str(), decision->reason.c_str(),
                    decision->predicted_ms,
                    static_cast<long long>(decision->predicted_bytes));
        return 0;
    }
    arrowlane::JsonValue root = arrowlane::JsonValue::make_object();
    root.set("lane", arrowlane::JsonValue(decision->lane));
    root.set("reason", arrowlane::JsonValue(decision->reason));
    root.set("predicted_ms", arrowlane::JsonValue(round4(decision->predicted_ms)));
    root.set("predicted_bytes", arrowlane::JsonValue(decision->predicted_bytes));
    arrowlane::JsonValue scores = arrowlane::JsonValue::make_array();
    for (const auto& [name, value] : decision->scores) {
        arrowlane::JsonValue pair = arrowlane::JsonValue::make_array();
        pair.push(arrowlane::JsonValue(name));
        pair.push(arrowlane::JsonValue(round4(value)));
        scores.push(std::move(pair));
    }
    root.set("scores", std::move(scores));
    arrowlane::JsonValue rejected = arrowlane::JsonValue::make_array();
    for (const auto& [name, why] : decision->rejected) {
        arrowlane::JsonValue pair = arrowlane::JsonValue::make_array();
        pair.push(arrowlane::JsonValue(name));
        pair.push(arrowlane::JsonValue(why));
        rejected.push(std::move(pair));
    }
    root.set("rejected", std::move(rejected));
    std::printf("%s\n", arrowlane::json_write(root).c_str());
    return 0;
}

int unpack_chunks_command(const char* path) {
    const auto bytes = read_file(path);
    if (bytes.empty()) {
        std::fprintf(stderr, "error: IoError: cannot read %s\n", path);
        return 1;
    }
    const std::span<const std::byte> all(bytes);
    arrowlane::ChunkAssembler assembler;
    std::size_t offset = 0;
    std::int64_t rows = 0;
    std::uint64_t chunks = 0;
    while (offset < all.size()) {
        auto size = arrowlane::frame_size(all.subspan(offset));
        if (!size) return fail(size.error());
        auto batch = assembler.add(all.subspan(offset, *size));
        if (!batch) return fail(batch.error());
        rows += batch->rows();
        ++chunks;
        offset += *size;
        if (assembler.finished()) break;
    }
    auto complete = assembler.check_complete();
    if (!complete) return fail(complete.error());
    if (offset != all.size()) {
        return fail(arrowlane::make_error(arrowlane::Code::Chunk,
                                          "trailing bytes after the last chunk"));
    }
    std::printf("chunks=%llu rows=%lld\n", static_cast<unsigned long long>(chunks),
                static_cast<long long>(rows));
    return 0;
}

int unpack_command(const char* path, bool as_json) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        std::fprintf(stderr, "error: IoError: cannot open %s\n", path);
        return 1;
    }
    stream.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(stream.tellg());
    stream.seekg(0, std::ios::beg);
    std::vector<std::byte> wire(size);
    if (size > 0) {
        stream.read(reinterpret_cast<char*>(wire.data()),
                    static_cast<std::streamsize>(size));
    }
    const std::size_t wire_size = wire.size();
    auto owner = std::make_shared<const std::vector<std::byte>>(std::move(wire));
    const std::span<const std::byte> data(owner->data(), owner->size());
    auto view = arrowlane::decode_frame_view(data);
    if (!view) return fail(view.error());
    auto decode = [&]() -> arrowlane::Result<arrowlane::RecordBatch> {
        if (view->lane == arrowlane::LaneId::Ipc) {
            return arrowlane::unpack_view(*view, owner);
        }
        auto frame = arrowlane::decode_frame(data);
        if (!frame) return std::unexpected(frame.error());
        return arrowlane::decode_lane(*frame);
    };
    auto batch = decode();
    if (!batch) return fail(batch.error());
    if (!as_json) {
        std::printf("lane %s rows %lld columns %zu\n",
                    std::string(arrowlane::lane_name(view->lane)).c_str(),
                    static_cast<long long>(batch->rows()), batch->columns.size());
        return 0;
    }
    arrowlane::JsonValue root = arrowlane::JsonValue::make_object();
    root.set("lane", arrowlane::JsonValue(std::string(arrowlane::lane_name(view->lane))));
    root.set("rows", arrowlane::JsonValue(batch->rows()));
    root.set("columns",
             arrowlane::JsonValue(static_cast<std::int64_t>(batch->columns.size())));
    root.set("uncompressed_bytes",
             arrowlane::JsonValue(static_cast<std::int64_t>(view->uncompressed_len)));
    root.set("wire_bytes", arrowlane::JsonValue(static_cast<std::int64_t>(wire_size)));
    arrowlane::JsonValue schema = arrowlane::JsonValue::make_array();
    for (std::size_t index = 0; index < batch->schema.fields.size(); ++index) {
        const auto& field = batch->schema.fields[index];
        arrowlane::JsonValue entry = arrowlane::JsonValue::make_object();
        entry.set("name", arrowlane::JsonValue(field.name));
        entry.set("type", arrowlane::JsonValue(field.type.name()));
        entry.set("nullable", arrowlane::JsonValue(field.nullable));
        entry.set("crc32", arrowlane::JsonValue::make_uint(arrowlane::column_digest(
                              batch->columns[index], field.type)));
        schema.push(std::move(entry));
    }
    root.set("schema", std::move(schema));
    std::printf("%s\n", arrowlane::json_write(root).c_str());
    return 0;
}

std::int64_t peak_rss_kb() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters)) != 0) {
        return static_cast<std::int64_t>(counters.PeakWorkingSetSize / 1024);
    }
    return 0;
#else
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmHWM:", 0) != 0) continue;
        std::istringstream text(line.substr(6));
        std::int64_t kilobytes = 0;
        text >> kilobytes;
        return kilobytes;
    }
    return 0;
#endif
}

std::string_view lane_media_type(arrowlane::LaneId lane) {
    switch (lane) {
        case arrowlane::LaneId::Json: return "application/x-ndjson";
        case arrowlane::LaneId::Ipc: return "application/vnd.apache.arrow.stream";
        case arrowlane::LaneId::IpcZstd: return "application/vnd.arrowlane.zstd";
        case arrowlane::LaneId::Shm: return "application/vnd.arrowlane.shm";
    }
    return "";
}

int describe_command(bool runtime_details) {
    // The manifest states what this build can do. `chunked` stays false until
    // the chunk stream lands in Phase 7, so a caller is never told a lie.
    arrowlane::JsonValue root = arrowlane::JsonValue::make_object();
    root.set("schema_version", arrowlane::JsonValue(std::int64_t{1}));
    root.set("package", arrowlane::JsonValue(std::string("arrowlane-cpp")));
    root.set("package_version", arrowlane::JsonValue(std::string(arrowlane::kVersion)));
    // The manifest must not depend on the host or the build, so
    // `describe --manifest` omits both the build type and per lane
    // availability. The committed file then matches on every platform.
    if (runtime_details) {
#if defined(AL_BUILD_TYPE)
        root.set("build_type", arrowlane::JsonValue(std::string(AL_BUILD_TYPE)));
#else
        root.set("build_type", arrowlane::JsonValue(std::string("unknown")));
#endif
    }
    arrowlane::JsonValue lanes = arrowlane::JsonValue::make_array();
    for (std::string_view name : arrowlane::lane_names()) {
        const auto id = arrowlane::lane_id_from_name(name);
        bool available = false;
        for (std::string_view candidate : arrowlane::available_lanes()) {
            if (candidate == name) available = true;
        }
        arrowlane::JsonValue entry = arrowlane::JsonValue::make_object();
        entry.set("name", arrowlane::JsonValue(std::string(name)));
        entry.set("id", arrowlane::JsonValue(
                            static_cast<std::int64_t>(id ? *id : arrowlane::LaneId::Json)));
        entry.set("media_type",
                  arrowlane::JsonValue(std::string(lane_media_type(
                      id.value_or(arrowlane::LaneId::Json)))));
        if (runtime_details) {
            entry.set("available", arrowlane::JsonValue(available));
        }
        lanes.push(std::move(entry));
    }
    root.set("lanes", std::move(lanes));
    arrowlane::JsonValue limits = arrowlane::JsonValue::make_object();
    limits.set("max_header_bytes",
               arrowlane::JsonValue(static_cast<std::int64_t>(
                   arrowlane::kMaxHeaderBytes)));
    limits.set("max_payload_bytes",
               arrowlane::JsonValue(static_cast<std::int64_t>(
                   arrowlane::kMaxPayloadBytes)));
    limits.set("max_uncompressed_bytes",
               arrowlane::JsonValue(static_cast<std::int64_t>(
                   arrowlane::kMaxUncompressedBytes)));
    limits.set("max_chunk_rows",
               arrowlane::JsonValue(static_cast<std::int64_t>(
                   arrowlane::kMaxChunkRows)));
    limits.set("max_chunks", arrowlane::JsonValue(static_cast<std::int64_t>(
                                 arrowlane::kMaxChunks)));
    root.set("limits", std::move(limits));
    arrowlane::JsonValue streaming = arrowlane::JsonValue::make_object();
    streaming.set("chunked", arrowlane::JsonValue(true));
    streaming.set("default_chunk_rows",
                  arrowlane::JsonValue(static_cast<std::int64_t>(
                      arrowlane::kDefaultChunkRows)));
    streaming.set("max_chunk_rows",
                  arrowlane::JsonValue(static_cast<std::int64_t>(
                      arrowlane::kMaxChunkRows)));
    root.set("streaming", std::move(streaming));
    std::printf("%s\n", arrowlane::json_write(root).c_str());
    return 0;
}

double elapsed_ms(std::chrono::steady_clock::time_point from,
                  std::chrono::steady_clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

int bench_command(std::int64_t rows, int repeats, bool as_json, bool machine) {
    if (rows < 0 || repeats < 1) {
        std::fprintf(stderr, "error: InvalidArgument: rows must be >= 0 and repeats >= 1\n");
        return 2;
    }
    const auto batch = arrowlane::make_fixture(rows);
    if (!as_json && !machine) {
        std::printf("rows=%lld repeats=%d\n", static_cast<long long>(rows), repeats);
        std::printf("%-10s %12s %12s %12s\n", "lane", "wire_bytes", "enc_ms", "dec_ms");
    }
    std::int64_t sink = 0;
    // The three codec lanes. shm moves bytes through a segment and needs a
    // token, so it is a transfer path, not a codec, and the reference harness
    // measures the same three.
    for (const std::string_view name : {"json", "ipc", "ipc-zstd"}) {
        const auto id = arrowlane::lane_id_from_name(name);
        if (!id) continue;
        bool available = false;
        for (std::string_view candidate : arrowlane::available_lanes()) {
            if (candidate == name) available = true;
        }
        if (!available) continue;
        auto frame = arrowlane::encode_lane(*id, batch);
        if (!frame) return fail(frame.error());
        auto wire = arrowlane::encode_frame(*frame);
        if (!wire) return fail(wire.error());
        const auto wire_bytes = static_cast<std::int64_t>(wire->size());
        std::vector<double> encode_samples;
        std::vector<double> decode_samples;
        encode_samples.reserve(static_cast<std::size_t>(repeats));
        decode_samples.reserve(static_cast<std::size_t>(repeats));
        for (int repeat = 0; repeat < repeats; ++repeat) {
            const auto start = std::chrono::steady_clock::now();
            auto encoded = arrowlane::encode_lane(*id, batch);
            if (!encoded) return fail(encoded.error());
            auto bytes = arrowlane::encode_frame(std::move(*encoded));
            if (!bytes) return fail(bytes.error());
            const auto encoded_at = std::chrono::steady_clock::now();
            auto decoded_frame = arrowlane::decode_frame(
                std::span<const std::byte>(bytes->data(), bytes->size()));
            if (!decoded_frame) return fail(decoded_frame.error());
            auto decoded = arrowlane::decode_lane(*decoded_frame);
            if (!decoded) return fail(decoded.error());
            const auto decoded_at = std::chrono::steady_clock::now();
            sink += decoded->rows();
            encode_samples.push_back(elapsed_ms(start, encoded_at));
            decode_samples.push_back(elapsed_ms(encoded_at, decoded_at));
        }
        const double encode_ms = median_of(std::move(encode_samples));
        const double decode_ms = median_of(std::move(decode_samples));
        if (machine) {
            // The benchmark protocol: one line per lane, four fields, so the
            // four implementations can be compared by one parser.
            std::printf("%s %lld %.3f %.3f\n", std::string(name).c_str(),
                        static_cast<long long>(wire_bytes), encode_ms, decode_ms);
            continue;
        }
        if (as_json) {
            arrowlane::JsonValue entry = arrowlane::JsonValue::make_object();
            entry.set("lane", arrowlane::JsonValue(std::string(name)));
            entry.set("wire_bytes", arrowlane::JsonValue(wire_bytes));
            entry.set("enc_ms", arrowlane::JsonValue(round4(encode_ms)));
            entry.set("dec_ms", arrowlane::JsonValue(round4(decode_ms)));
            entry.set("peak_rss_kb", arrowlane::JsonValue(peak_rss_kb()));
            std::printf("%s\n", arrowlane::json_write(entry).c_str());
            continue;
        }
        std::printf("%-10s %12lld %12.3f %12.3f\n",
                    std::string(name).c_str(), static_cast<long long>(wire_bytes),
                    encode_ms, decode_ms);
    }
    if (sink < 0) std::fprintf(stderr, "unreachable %lld\n", static_cast<long long>(sink));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("%s\n", arrowlane::kVersion);
        return 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "select") == 0) {
        std::int64_t rows = 0;
        std::int64_t bytes = 0;
        bool have_rows = false;
        bool have_bytes = false;
        const char* topology = nullptr;
        const char* policy = nullptr;
        const char* allow = nullptr;
        const char* model = nullptr;
        bool shm_ready = false;
        bool as_json = false;
        for (int index = 2; index < argc; ++index) {
            const std::string_view flag = argv[index];
            auto value = [&index, argc, argv]() -> const char* {
                return index + 1 < argc ? argv[++index] : nullptr;
            };
            if (flag == "--rows") {
                const char* text = value();
                if (text == nullptr) return usage();
                rows = std::strtoll(text, nullptr, 10);
                have_rows = true;
            } else if (flag == "--bytes") {
                const char* text = value();
                if (text == nullptr) return usage();
                bytes = std::strtoll(text, nullptr, 10);
                have_bytes = true;
            } else if (flag == "--topology") {
                topology = value();
            } else if (flag == "--policy") {
                policy = value();
            } else if (flag == "--allow") {
                allow = value();
            } else if (flag == "--model") {
                model = value();
            } else if (flag == "--shm-ready") {
                shm_ready = true;
            } else if (flag == "--json") {
                as_json = true;
            } else {
                return usage();
            }
        }
        if (!have_rows || !have_bytes) return usage();
        return select_command(rows, bytes, topology, policy, allow, shm_ready, model,
                              as_json);
    }
    if (argc >= 2 && std::strcmp(argv[1], "describe") == 0) {
        bool runtime_details = true;
        for (int index = 2; index < argc; ++index) {
            if (std::strcmp(argv[index], "--compact") == 0) continue;
            if (std::strcmp(argv[index], "--manifest") == 0) {
                runtime_details = false;
                continue;
            }
            return usage();
        }
        return describe_command(runtime_details);
    }
    if (argc >= 2 && std::strcmp(argv[1], "bench") == 0) {
        std::int64_t rows = 2000;
        int repeats = 3;
        bool as_json = false;
        bool machine = false;
        for (int index = 2; index < argc; ++index) {
            const std::string_view flag = argv[index];
            if (flag == "--json") {
                as_json = true;
                continue;
            }
            if (flag == "--machine") {
                machine = true;
                continue;
            }
            if (index + 1 >= argc) return usage();
            const char* text = argv[++index];
            if (flag == "--rows") rows = std::strtoll(text, nullptr, 10);
            else if (flag == "--repeats") repeats = static_cast<int>(std::strtol(text, nullptr, 10));
            else return usage();
        }
        return bench_command(rows, repeats, as_json, machine);
    }
    if (argc >= 2 && std::strcmp(argv[1], "pack") == 0) {
        const char* lane = nullptr;
        const char* out_path = nullptr;
        const char* in_path = nullptr;
        std::int64_t rows = 3;
        std::int64_t rows_per_chunk =
            static_cast<std::int64_t>(arrowlane::kDefaultChunkRows);
        bool chunks = false;
        bool dictionary = false;
        for (int index = 2; index < argc; ++index) {
            const std::string_view flag = argv[index];
            if (flag == "--chunks") {
                chunks = true;
                continue;
            }
            if (flag == "--dictionary") {
                dictionary = true;
                continue;
            }
            if (index + 1 >= argc) return usage();
            const char* text = argv[++index];
            if (flag == "--lane") lane = text;
            else if (flag == "--rows") rows = std::strtoll(text, nullptr, 10);
            else if (flag == "--out") out_path = text;
            else if (flag == "--in") in_path = text;
            else if (flag == "--rows-per-chunk") rows_per_chunk = std::strtoll(text, nullptr, 10);
            else return usage();
        }
        if (!lane || !out_path) return usage();
        return pack_command(lane, rows, out_path, in_path, chunks, rows_per_chunk,
                            dictionary);
    }
    if (argc >= 3 && std::strcmp(argv[1], "unpack") == 0) {
        // Flags may come before or after the path.
        const char* path = nullptr;
        bool as_json = false;
        bool chunks = false;
        for (int index = 2; index < argc; ++index) {
            const std::string_view flag = argv[index];
            if (flag == "--json") {
                as_json = true;
                continue;
            }
            if (flag == "--chunks") {
                chunks = true;
                continue;
            }
            if (path != nullptr) return usage();
            path = argv[index];
        }
        if (path == nullptr) return usage();
        if (chunks) return unpack_chunks_command(path);
        return unpack_command(path, as_json);
    }
    if (argc >= 3 && std::strcmp(argv[1], "debug-schema") == 0) {
        const std::string_view case_name = argv[2];
        if (case_name == "one") return emit(one_schema());
        if (case_name == "fixture") return emit(fixture_schema());
        if (case_name == "types") return emit(types_schema());
        if (case_name == "read" && argc == 4) return read_message(argv[3]);
        return usage();
    }
    return usage();
}
