#include "arrowlane/costmodel.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "arrowlane/json.hpp"

namespace arrowlane {
namespace {

Error config(std::string message) {
    return make_error(Code::Config, std::move(message));
}

Error not_a_number(const std::string& what) {
    return config(what + " must be a number");
}

Result<double> number_of(const JsonValue* value, const std::string& what) {
    if (value == nullptr || value->kind() != JsonValue::Kind::Number) {
        return std::unexpected(not_a_number(what));
    }
    const double number = value->is_integer()
                              ? (value->is_unsigned()
                                     ? static_cast<double>(value->as_uint())
                                     : static_cast<double>(value->as_int()))
                              : value->as_number();
    if (!std::isfinite(number)) {
        return std::unexpected(config(what + " must be finite"));
    }
    if (number < 0.0) {
        return std::unexpected(config(what + " must not be negative"));
    }
    return number;
}

const JsonValue* find_field(const JsonValue& object, std::string_view name) {
    return object.find(name);
}

std::string smallest(const std::vector<std::string>& names) {
    if (names.empty()) return {};
    return *std::min_element(names.begin(), names.end());
}

bool is_top_key(std::string_view key) {
    return key == "schema_version" || key == "provenance" || key == "wire_us_per_mb" ||
           key == "lanes";
}

bool is_lane_key(std::string_view key) {
    return key == "fixed_ms" || key == "enc_ms_per_mb" || key == "dec_ms_per_mb" ||
           key == "ratio";
}

}  // namespace

Result<CostModel> CostModel::parse(std::string_view json) {
    auto parsed = json_parse(json);
    if (!parsed) {
        return std::unexpected(config("cost model is not JSON"));
    }
    if (!parsed->is_object()) {
        return std::unexpected(config("cost model must be a JSON object"));
    }
    std::vector<std::string> unknown;
    for (const auto& [key, value] : parsed->fields()) {
        (void)value;
        if (!is_top_key(key)) unknown.push_back(key);
    }
    if (!unknown.empty()) {
        return std::unexpected(config("unknown key: " + smallest(unknown)));
    }
    const JsonValue* version = find_field(*parsed, "schema_version");
    const bool version_ok =
        version != nullptr && version->kind() == JsonValue::Kind::Number &&
        (version->is_integer() ? static_cast<double>(version->as_int())
                               : version->as_number()) == 1.0;
    if (!version_ok) {
        return std::unexpected(config("schema_version must be 1"));
    }
    const JsonValue* provenance = find_field(*parsed, "provenance");
    if (provenance == nullptr || !provenance->is_object() ||
        provenance->fields().empty()) {
        return std::unexpected(config("provenance must be a non-empty object"));
    }
    const JsonValue* wire = find_field(*parsed, "wire_us_per_mb");
    if (wire == nullptr || !wire->is_object()) {
        return std::unexpected(config("wire_us_per_mb must be an object"));
    }
    CostModel model;
    model.schema_version_ = 1;
    auto same_host = number_of(find_field(*wire, "same_host"), "wire_us_per_mb.same_host");
    if (!same_host) return std::unexpected(same_host.error());
    auto same_network =
        number_of(find_field(*wire, "same_network"), "wire_us_per_mb.same_network");
    if (!same_network) return std::unexpected(same_network.error());
    auto wide_area = number_of(find_field(*wire, "wide_area"), "wire_us_per_mb.wide_area");
    if (!wide_area) return std::unexpected(wide_area.error());
    model.same_host_ = *same_host;
    model.same_network_ = *same_network;
    model.wide_area_ = *wide_area;

    const JsonValue* lanes = find_field(*parsed, "lanes");
    if (lanes == nullptr || !lanes->is_object() || lanes->fields().empty()) {
        return std::unexpected(config("lanes must be a non-empty object"));
    }
    for (const auto& [name, entry] : lanes->fields()) {
        if (!entry.is_object()) {
            return std::unexpected(config("lane " + name + " must be an object"));
        }
        std::vector<std::string> extra;
        for (const auto& [key, value] : entry.fields()) {
            (void)value;
            if (!is_lane_key(key)) extra.push_back(key);
        }
        if (!extra.empty()) {
            return std::unexpected(
                config("lane " + name + " has unknown key " + smallest(extra)));
        }
        std::vector<std::string> missing;
        for (const char* key : {"fixed_ms", "enc_ms_per_mb", "dec_ms_per_mb", "ratio"}) {
            if (find_field(entry, key) == nullptr) missing.emplace_back(key);
        }
        if (!missing.empty()) {
            return std::unexpected(
                config("lane " + name + " is missing " + smallest(missing)));
        }
        LaneCost cost;
        auto fixed = number_of(find_field(entry, "fixed_ms"), "lanes." + name + ".fixed_ms");
        if (!fixed) return std::unexpected(fixed.error());
        auto enc =
            number_of(find_field(entry, "enc_ms_per_mb"), "lanes." + name + ".enc_ms_per_mb");
        if (!enc) return std::unexpected(enc.error());
        auto dec =
            number_of(find_field(entry, "dec_ms_per_mb"), "lanes." + name + ".dec_ms_per_mb");
        if (!dec) return std::unexpected(dec.error());
        auto ratio = number_of(find_field(entry, "ratio"), "lanes." + name + ".ratio");
        if (!ratio) return std::unexpected(ratio.error());
        cost.fixed_ms = *fixed;
        cost.enc_ms_per_mb = *enc;
        cost.dec_ms_per_mb = *dec;
        cost.ratio = *ratio;
        model.lanes_.push_back(Entry{name, cost});
    }
    if (model.find("json") == nullptr) {
        return std::unexpected(
            config("cost model must carry the json lane as the baseline"));
    }
    return model;
}

Result<CostModel> CostModel::from_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::unexpected(make_error(Code::Io, "cannot read " + path));
    }
    std::string text((std::istreambuf_iterator<char>(stream)),
                     std::istreambuf_iterator<char>());
    return parse(text);
}

Result<CostModel> CostModel::builtin() {
    static const Result<CostModel> cached = parse(builtin_cost_model_json());
    return cached;
}

const LaneCost* CostModel::find(std::string_view lane) const noexcept {
    for (const Entry& entry : lanes_) {
        if (entry.name == lane) return &entry.cost;
    }
    return nullptr;
}

double CostModel::wire_us_per_mb(std::string_view topology) const {
    if (topology == "same_host") return same_host_;
    if (topology == "same_network") return same_network_;
    if (topology == "wide_area") return wide_area_;
    return 0.0;
}

double CostModel::predict(std::string_view lane, double mb, std::string_view topology,
                          std::string_view policy) const {
    const LaneCost* cost = find(lane);
    if (cost == nullptr) return 0.0;
    double total = cost->fixed_ms + (cost->enc_ms_per_mb + cost->dec_ms_per_mb) * mb;
    if (policy != "cpu") {
        total += (wire_us_per_mb(topology) / 1000.0) * mb * cost->ratio;
    }
    return total;
}

std::int64_t CostModel::bytes_for(std::string_view lane, std::int64_t arrow_bytes) const {
    const LaneCost* cost = find(lane);
    if (cost == nullptr) return 0;
    return static_cast<std::int64_t>(static_cast<double>(arrow_bytes) * cost->ratio);
}

}  // namespace arrowlane
