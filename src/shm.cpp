#include "arrowlane/shm.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "arrowlane/crc32.hpp"
#include "arrowlane/json.hpp"

#if !defined(_WIN32)
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <ctime>
#endif

namespace arrowlane {
namespace {

Error segment_error(std::string message) {
    return make_error(Code::Segment, std::move(message));
}

// Constant time for equal lengths: every byte is compared.
bool same_token(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    unsigned char difference = 0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        difference |= static_cast<unsigned char>(left[index] ^ right[index]);
    }
    return difference == 0;
}

std::string make_segment_name() {
    static const std::uint64_t seed = [] {
        std::random_device device;
        const std::uint64_t high = static_cast<std::uint64_t>(device()) << 32;
        return high ^ static_cast<std::uint64_t>(device());
    }();
    static std::uint64_t counter = 0;
    ++counter;
    char text[35];
    std::memcpy(text, kShmNamePrefix.data(), kShmNamePrefix.size());
    std::size_t cursor = kShmNamePrefix.size();
    for (int index = 0; index < 32; ++index) {
        const std::uint64_t source = index < 16 ? seed : counter;
        const int shift = 60 - 4 * (index % 16);
        const int nibble = static_cast<int>((source >> shift) & 0x0F);
        text[cursor++] = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
    return std::string(text, cursor);
}

std::string number_text(std::uint64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    return buffer;
}

bool is_integer(const JsonValue* value) {
    return value != nullptr && value->kind() == JsonValue::Kind::Number &&
           value->is_integer();
}

}  // namespace

std::string ShmDescriptor::to_json() const {
    // Sorted keys, no spaces, so two writers produce the same bytes.
    std::string text = "{\"crc32\":";
    text += number_text(crc32);
    text += ",\"length\":";
    text += number_text(length);
    text += ",\"name\":\"";
    text += name;
    text += "\",\"offset\":";
    text += number_text(offset);
    text += ",\"token\":\"";
    text += token;
    text += "\"}";
    return text;
}

Result<ShmDescriptor> ShmDescriptor::from_json(std::string_view text) {
    auto parsed = json_parse(text);
    if (!parsed) return std::unexpected(segment_error("descriptor is not UTF-8 JSON"));
    if (!parsed->is_object()) {
        return std::unexpected(segment_error("descriptor is not an object"));
    }
    for (const auto& [key, value] : parsed->fields()) {
        (void)value;
        if (key != "name" && key != "length" && key != "crc32" && key != "token" &&
            key != "offset") {
            return std::unexpected(segment_error("descriptor has unknown keys"));
        }
    }
    const JsonValue* name = parsed->find("name");
    if (name == nullptr || name->kind() != JsonValue::Kind::String ||
        name->as_string().rfind(std::string(kShmNamePrefix), 0) != 0 ||
        name->as_string().find('/') != std::string::npos) {
        return std::unexpected(segment_error("descriptor name is invalid"));
    }
    const JsonValue* length = parsed->find("length");
    if (!is_integer(length) || length->as_int() <= 0 ||
        static_cast<std::uint64_t>(length->as_int()) > kMaxSegmentBytes) {
        return std::unexpected(segment_error("descriptor length is invalid"));
    }
    const JsonValue* offset = parsed->find("offset");
    if (offset != nullptr) {
        if (!is_integer(offset) || offset->as_int() < 0) {
            return std::unexpected(segment_error("descriptor offset is invalid"));
        }
    }
    const JsonValue* crc = parsed->find("crc32");
    if (!is_integer(crc) || crc->as_int() < 0 ||
        static_cast<std::uint64_t>(crc->as_int()) > 0xFFFFFFFFu) {
        return std::unexpected(segment_error("descriptor crc32 is invalid"));
    }
    const JsonValue* token = parsed->find("token");
    if (token == nullptr || token->kind() != JsonValue::Kind::String ||
        token->as_string().size() < kMinTokenChars) {
        return std::unexpected(segment_error("descriptor token is invalid"));
    }
    ShmDescriptor descriptor;
    descriptor.name = name->as_string();
    descriptor.length = static_cast<std::uint64_t>(length->as_int());
    descriptor.crc32 = static_cast<std::uint32_t>(crc->as_int());
    descriptor.token = token->as_string();
    descriptor.offset =
        offset == nullptr ? 0 : static_cast<std::uint64_t>(offset->as_int());
    return descriptor;
}

#if !defined(_WIN32)

std::string segment_directory(const std::string& directory) {
    if (!directory.empty()) return directory;
    if (const char* override_path = std::getenv("ARROWLAND_SHM_DIR");
        override_path != nullptr && override_path[0] != '\0') {
        return override_path;
    }
    struct stat info {};
    if (::stat("/dev/shm", &info) == 0 && S_ISDIR(info.st_mode)) return "/dev/shm";
    return "/tmp";
}

bool is_ready(const std::string& directory) {
    const std::string target = segment_directory(directory);
    struct stat info {};
    if (::stat(target.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) return false;
    const std::string probe = target + "/" + make_segment_name() + "-probe";
    const int fd = ::open(probe.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    ::close(fd);
    ::unlink(probe.c_str());
    return true;
}

Result<ShmDescriptor> write_segment(std::span<const std::byte> data,
                                    const std::string& directory,
                                    const std::string& token,
                                    std::size_t max_attempts) {
    if (data.empty()) {
        return std::unexpected(segment_error("refusing to write an empty segment"));
    }
    if (data.size() > kMaxSegmentBytes) {
        return std::unexpected(segment_error("segment " + std::to_string(data.size()) +
                                             " exceeds " +
                                             std::to_string(kMaxSegmentBytes)));
    }
    if (max_attempts == 0) {
        return std::unexpected(make_error(Code::Invalid, "max_attempts must be positive"));
    }
    const std::string target = segment_directory(directory);
    const std::string name = make_segment_name();
    const std::string path = target + "/" + name;
    const int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0) {
        return std::unexpected(
            segment_error("cannot create a segment: " + std::string(std::strerror(errno))));
    }
    std::size_t written = 0;
    bool failed = false;
    for (std::size_t attempt = 0; attempt < max_attempts && written < data.size();
         ++attempt) {
        const auto chunk = ::write(fd, data.data() + written, data.size() - written);
        if (chunk <= 0) {
            failed = true;
            break;
        }
        written += static_cast<std::size_t>(chunk);
    }
    const bool complete = !failed && written == data.size();
    ::close(fd);
    if (!complete) {
        ::unlink(path.c_str());
        return std::unexpected(segment_error("segment write did not finish at " +
                                             std::to_string(written) + " of " +
                                             std::to_string(data.size()) + " bytes"));
    }
    ShmDescriptor descriptor;
    descriptor.name = name;
    descriptor.length = data.size();
    descriptor.crc32 = crc32(data);
    descriptor.token = token;
    return descriptor;
}

Result<std::vector<std::byte>> read_segment(const ShmDescriptor& descriptor,
                                            const std::string& directory,
                                            std::string_view token, bool verify) {
    if (!same_token(descriptor.token, token)) {
        return std::unexpected(make_error(Code::Auth, "segment token does not match"));
    }
    const std::string path = segment_directory(directory) + "/" + descriptor.name;
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(
            make_error(Code::SegmentGone, "cannot open segment " + descriptor.name));
    }
    struct stat info {};
    if (::fstat(fd, &info) != 0) {
        ::close(fd);
        return std::unexpected(
            make_error(Code::Segment, "cannot stat segment " + descriptor.name));
    }
    if (static_cast<std::uint64_t>(info.st_size) != descriptor.length) {
        ::close(fd);
        return std::unexpected(
            make_error(Code::Segment, "segment size does not match the descriptor"));
    }
    void* mapping = ::mmap(nullptr, descriptor.length, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapping == MAP_FAILED) {
        ::close(fd);
        return std::unexpected(
            make_error(Code::Segment, "cannot map segment " + descriptor.name));
    }
    // The name goes as soon as the mapping exists; Linux keeps the pages alive
    // until the last mapping is dropped.
    ::unlink(path.c_str());
    const auto bytes = std::span<const std::byte>(static_cast<const std::byte*>(mapping),
                                                 static_cast<std::size_t>(descriptor.length));
    const bool crc_ok = !verify || crc32(bytes) == descriptor.crc32;
    std::vector<std::byte> copy;
    if (crc_ok) copy.assign(bytes.begin(), bytes.end());
    ::munmap(mapping, descriptor.length);
    ::close(fd);
    if (!crc_ok) {
        return std::unexpected(
            make_error(Code::ChecksumMismatch, "segment does not match its CRC"));
    }
    return copy;
}

Result<void> discard(const ShmDescriptor& descriptor, const std::string& directory) {
    const std::string path = segment_directory(directory) + "/" + descriptor.name;
    if (::unlink(path.c_str()) != 0 && errno != ENOENT) {
        return std::unexpected(
            make_error(Code::Segment, "cannot remove segment " + descriptor.name));
    }
    return {};
}

Result<std::size_t> sweep(const std::string& directory, double max_age_seconds) {
    const std::string target = segment_directory(directory);
    DIR* handle = ::opendir(target.c_str());
    if (handle == nullptr) {
        return std::unexpected(make_error(Code::Segment, "cannot read " + target));
    }
    const auto now = static_cast<double>(std::time(nullptr));
    std::size_t removed = 0;
    while (const dirent* entry = ::readdir(handle)) {
        const std::string name = entry->d_name;
        if (name.rfind(std::string(kShmNamePrefix), 0) != 0) continue;
        const std::string path = target + "/" + name;
        struct stat info {};
        if (::stat(path.c_str(), &info) != 0) continue;
        if (!S_ISREG(info.st_mode)) continue;
        const double age = now - static_cast<double>(info.st_mtime);
        if (age <= max_age_seconds) continue;
        if (::unlink(path.c_str()) == 0) ++removed;
    }
    ::closedir(handle);
    return removed;
}

#else

namespace {

Error unavailable() {
    return make_error(Code::LaneUnavailable,
                      "the shm lane requires a POSIX platform");
}

}  // namespace

std::string segment_directory(const std::string& directory) { return directory; }

bool is_ready(const std::string& /*directory*/) { return false; }

Result<ShmDescriptor> write_segment(std::span<const std::byte> /*data*/,
                                    const std::string& /*directory*/,
                                    const std::string& /*token*/,
                                    std::size_t /*max_attempts*/) {
    return std::unexpected(unavailable());
}

Result<std::vector<std::byte>> read_segment(const ShmDescriptor& /*descriptor*/,
                                            const std::string& /*directory*/,
                                            std::string_view /*token*/,
                                            bool /*verify*/) {
    return std::unexpected(unavailable());
}

Result<void> discard(const ShmDescriptor& /*descriptor*/,
                     const std::string& /*directory*/) {
    return std::unexpected(unavailable());
}

Result<std::size_t> sweep(const std::string& /*directory*/, double /*max_age_seconds*/) {
    return std::unexpected(unavailable());
}

#endif

}  // namespace arrowlane
