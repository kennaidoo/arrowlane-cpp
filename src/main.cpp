#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "arrowlane/digest.hpp"
#include "arrowlane/fixture.hpp"
#include "arrowlane/lanes.hpp"
#include "arrowlane/schema.hpp"
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
                 "  arrowlane-cpp pack --lane L --rows N --out FILE\n"
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

int pack_command(const char* lane_text, std::int64_t rows, const char* out_path) {
    const auto lane = arrowlane::lane_id_from_name(lane_text);
    if (!lane) {
        std::fprintf(stderr, "error: UnknownLane: no lane named %s\n", lane_text);
        return 1;
    }
    const auto batch = arrowlane::make_fixture(rows);
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
    if (argc >= 2 && std::strcmp(argv[1], "pack") == 0) {
        const char* lane = nullptr;
        const char* out_path = nullptr;
        std::int64_t rows = 3;
        for (int index = 2; index + 1 < argc; index += 2) {
            const std::string_view flag = argv[index];
            if (flag == "--lane") lane = argv[index + 1];
            else if (flag == "--rows") rows = std::strtoll(argv[index + 1], nullptr, 10);
            else if (flag == "--out") out_path = argv[index + 1];
            else return usage();
        }
        if (!lane || !out_path) return usage();
        return pack_command(lane, rows, out_path);
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
