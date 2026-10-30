#include "arrowlane/cabi.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "arrowlane/column.hpp"
#include "arrowlane/costmodel.hpp"
#include "arrowlane/digest.hpp"
#include "arrowlane/frame.hpp"
#include "arrowlane/json.hpp"
#include "arrowlane/lanes.hpp"
#include "arrowlane/select.hpp"
#include "arrowlane/version.hpp"

namespace {

// Every entry point is wrapped, so no exception crosses the C boundary.
// A failed call returns an empty buffer or AL_ERR_PANIC.

char* duplicate(const std::string& text) {
    auto* copy = static_cast<char*>(std::malloc(text.size() + 1));
    if (copy != nullptr) std::memcpy(copy, text.c_str(), text.size() + 1);
    return copy;
}

AlBuffer empty_buffer() { return AlBuffer{nullptr, 0, 0}; }

AlBuffer make_buffer(const std::string& text) {
    if (text.empty()) return empty_buffer();
    auto* data = static_cast<uint8_t*>(std::malloc(text.size()));
    if (data == nullptr) return empty_buffer();
    std::memcpy(data, text.data(), text.size());
    return AlBuffer{data, text.size(), text.size()};
}

bool known_lane(uint8_t lane_id) {
    const auto lane = static_cast<arrowlane::LaneId>(lane_id);
    return arrowlane::lane_id_from_name(arrowlane::lane_name(lane)).has_value();
}

}  // namespace

extern "C" {

const char* al_version(void) { return arrowlane::kVersion; }

int al_select(uint64_t rows, uint64_t arrow_bytes, uint8_t topology, uint8_t policy,
              bool shm_ready, AlDecision* out) {
    try {
        if (out == nullptr) return AL_ERR_INVALID;
        if (topology > 2 || policy > 2) return AL_ERR_INVALID;
        constexpr auto kLimit = static_cast<uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (rows > kLimit || arrow_bytes > kLimit) return AL_ERR_INVALID;
        auto model = arrowlane::CostModel::builtin();
        if (!model) return AL_ERR_INVALID;
        arrowlane::Workload workload;
        workload.rows = static_cast<std::int64_t>(rows);
        workload.arrow_bytes = static_cast<std::int64_t>(arrow_bytes);
        auto decision = arrowlane::select_plan(
            workload, static_cast<arrowlane::Topology>(topology),
            static_cast<arrowlane::Policy>(policy), *model, {}, shm_ready);
        if (!decision) return AL_ERR_INVALID;
        const auto lane = arrowlane::lane_id_from_name(decision->lane);
        if (!lane) return AL_ERR_INVALID;
        char* reason = duplicate(decision->reason);
        if (reason == nullptr) return AL_ERR_PANIC;
        out->lane_id = static_cast<uint8_t>(*lane);
        out->predicted_ms = decision->predicted_ms;
        out->predicted_bytes = static_cast<uint64_t>(decision->predicted_bytes);
        out->reason = reason;
        return AL_OK;
    } catch (...) {
        return AL_ERR_PANIC;
    }
}

AlBuffer al_inspect(const uint8_t* data, size_t len) {
    try {
        if (data == nullptr || len == 0) return empty_buffer();
        const auto bytes =
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), len);
        auto frame = arrowlane::decode_frame(bytes);
        if (!frame) return empty_buffer();
        auto batch = arrowlane::decode_lane(*frame);
        if (!batch) return empty_buffer();
        arrowlane::JsonValue root = arrowlane::JsonValue::make_object();
        root.set("lane", arrowlane::JsonValue(
                             std::string(arrowlane::lane_name(frame->lane))));
        root.set("rows", arrowlane::JsonValue(batch->rows()));
        root.set("columns", arrowlane::JsonValue(
                                static_cast<std::int64_t>(batch->columns.size())));
        root.set("wire_bytes", arrowlane::JsonValue(static_cast<std::int64_t>(len)));
        root.set("uncompressed_bytes",
                 arrowlane::JsonValue(static_cast<std::int64_t>(frame->uncompressed_len)));
        arrowlane::JsonValue digests = arrowlane::JsonValue::make_array();
        for (std::size_t index = 0; index < batch->columns.size(); ++index) {
            arrowlane::JsonValue entry = arrowlane::JsonValue::make_object();
            entry.set("name", arrowlane::JsonValue(batch->schema.fields[index].name));
            entry.set("crc32",
                      arrowlane::JsonValue::make_uint(arrowlane::column_digest(
                          batch->columns[index], batch->schema.fields[index].type)));
            digests.push(std::move(entry));
        }
        root.set("digests", std::move(digests));
        return make_buffer(arrowlane::json_write(root));
    } catch (...) {
        return empty_buffer();
    }
}

AlBuffer al_repack(const uint8_t* data, size_t len, uint8_t lane_id) {
    try {
        if (data == nullptr || len == 0) return empty_buffer();
        if (!known_lane(lane_id)) return empty_buffer();
        const auto bytes =
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), len);
        auto frame = arrowlane::decode_frame(bytes);
        if (!frame) return empty_buffer();
        auto batch = arrowlane::decode_lane(*frame);
        if (!batch) return empty_buffer();
        auto encoded = arrowlane::encode_lane(static_cast<arrowlane::LaneId>(lane_id),
                                              *batch);
        if (!encoded) return empty_buffer();
        auto wire = arrowlane::encode_frame(*encoded);
        if (!wire) return empty_buffer();
        auto* copy = static_cast<uint8_t*>(std::malloc(wire->size()));
        if (copy == nullptr) return empty_buffer();
        std::memcpy(copy, wire->data(), wire->size());
        return AlBuffer{copy, wire->size(), wire->size()};
    } catch (...) {
        return empty_buffer();
    }
}

void al_free(AlBuffer buffer) {
    std::free(buffer.data);
}

void al_free_reason(const char* reason) {
    // The pointer comes from duplicate(), so it is a plain malloc block.
    std::free(const_cast<char*>(reason));
}

}  // extern "C"
