#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "arrowlane/costmodel.hpp"
#include "arrowlane/digest.hpp"
#include "arrowlane/fixture.hpp"
#include "arrowlane/ipc.hpp"
#include "arrowlane/lanes.hpp"
#include "arrowlane/schema.hpp"
#include "arrowlane/select.hpp"
#include "arrowlane/version.hpp"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {

arrowlane::Schema one_schema() {
    arrowlane::Schema schema;
    schema.fields.push_back(arrowlane::Field{"a", arrowlane::Type::integer(64, true), false});
    return schema;
}

arrowlane::Schema fixture_schema() {
    arrowlane::Schema schema;
    schema.fields.push_back(arrowlane::Field{"id", arrowlane::Type::integer(64, true), true});
    schema.fields.push_back(arrowlane::Field{"name", arrowlane::Type::utf8(), true});
    schema.fields.push_back(arrowlane::Field{"score", arrowlane::Type::floating(64), true});
    schema.fields.push_back(arrowlane::Field{"ok", arrowlane::Type::boolean(), true});
    schema.fields.push_back(arrowlane::Field{"day", arrowlane::Type::date(0), true});
    schema.fields.push_back(
        arrowlane::Field{"amount", arrowlane::Type::decimal(9, 2, 128), true});
    schema.fields.push_back(
        arrowlane::Field{"ts", arrowlane::Type::timestamp(2, "UTC"), true});
    schema.fields.push_back(arrowlane::Field{"blob", arrowlane::Type::binary(), true});
    return schema;
}

arrowlane::Schema types_schema() {
    arrowlane::Schema schema;
    schema.fields.push_back(arrowlane::Field{"n", arrowlane::Type::null(), true});
    schema.fields.push_back(arrowlane::Field{"lb", arrowlane::Type::large_binary(), false});
    schema.fields.push_back(arrowlane::Field{"lu", arrowlane::Type::large_utf8(), false});
    schema.fields.push_back(arrowlane::Field{"u32", arrowlane::Type::integer(32, false), false});
    schema.fields.push_back(arrowlane::Field{"f16", arrowlane::Type::floating(16), false});
    schema.fields.push_back(arrowlane::Field{"d64", arrowlane::Type::date(1), false});
    schema.fields.push_back(arrowlane::Field{"t32", arrowlane::Type::time(1, 32), false});
    schema.fields.push_back(arrowlane::Field{"t64", arrowlane::Type::time(3, 64), false});
    schema.fields.push_back(arrowlane::Field{"ts", arrowlane::Type::timestamp(0), false});
    schema.fields.push_back(arrowlane::Field{"dur", arrowlane::Type::duration(1), false});
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
                 "  arrowlane-cpp pack --lane L [--rows N | --in FILE] --out FILE\n"
                 "  arrowlane-cpp unpack FILE [--json]\n"
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
                 const char* in_path) {
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
    } else {
        batch = arrowlane::make_fixture(rows);
    }
    auto frame = arrowlane::encode_lane(*lane, batch);
    if (!frame) return fail(frame.error());
    auto wire = arrowlane::encode_frame(*frame);
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
    if (argc >= 2 && std::strcmp(argv[1], "pack") == 0) {
        const char* lane = nullptr;
        const char* out_path = nullptr;
        const char* in_path = nullptr;
        std::int64_t rows = 3;
        for (int index = 2; index + 1 < argc; index += 2) {
            const std::string_view flag = argv[index];
            if (flag == "--lane") lane = argv[index + 1];
            else if (flag == "--rows") rows = std::strtoll(argv[index + 1], nullptr, 10);
            else if (flag == "--out") out_path = argv[index + 1];
            else if (flag == "--in") in_path = argv[index + 1];
            else return usage();
        }
        if (!lane || !out_path) return usage();
        return pack_command(lane, rows, out_path, in_path);
    }
    if (argc >= 3 && std::strcmp(argv[1], "unpack") == 0) {
        bool as_json = false;
        if (argc == 4 && std::strcmp(argv[3], "--json") == 0) as_json = true;
        if (argc > 4) return usage();
        return unpack_command(argv[2], as_json);
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
