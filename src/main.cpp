#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

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
                 "  arrowlane-cpp debug-schema one|fixture|types\n"
                 "  arrowlane-cpp debug-schema read FILE\n",
                 arrowlane::kVersion);
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("%s\n", arrowlane::kVersion);
        return 0;
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
