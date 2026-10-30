#include "arrowlane/select.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "arrowlane/lanes.hpp"

namespace arrowlane {
namespace {

int tie_order(std::string_view lane) {
    if (lane == "shm") return 0;
    if (lane == "ipc-zstd") return 1;
    if (lane == "ipc") return 2;
    if (lane == "json") return 3;
    return 99;
}

bool platform_has(std::string_view lane) {
    for (std::string_view name : available_lanes()) {
        if (name == lane) return true;
    }
    return false;
}

}  // namespace

std::string_view topology_name(Topology topology) noexcept {
    switch (topology) {
        case Topology::SameHost: return "same_host";
        case Topology::SameNetwork: return "same_network";
        case Topology::WideArea: return "wide_area";
    }
    return "";
}

std::string_view policy_name(Policy policy) noexcept {
    switch (policy) {
        case Policy::Latency: return "latency";
        case Policy::Throughput: return "throughput";
        case Policy::Cpu: return "cpu";
    }
    return "";
}

bool parse_topology(std::string_view text, Topology& out) noexcept {
    if (text == "same_host") {
        out = Topology::SameHost;
        return true;
    }
    if (text == "same_network") {
        out = Topology::SameNetwork;
        return true;
    }
    if (text == "wide_area") {
        out = Topology::WideArea;
        return true;
    }
    return false;
}

bool parse_policy(std::string_view text, Policy& out) noexcept {
    if (text == "latency") {
        out = Policy::Latency;
        return true;
    }
    if (text == "throughput") {
        out = Policy::Throughput;
        return true;
    }
    if (text == "cpu") {
        out = Policy::Cpu;
        return true;
    }
    return false;
}

Result<Decision> select_plan(const Workload& workload, Topology topology, Policy policy,
                             const CostModel& model,
                             const std::vector<std::string>& allowed, bool shm_ready,
                             double min_gain) {
    if (workload.rows < 0) {
        return std::unexpected(make_error(Code::Invalid, "rows must not be negative"));
    }
    if (workload.arrow_bytes < 0) {
        return std::unexpected(
            make_error(Code::Invalid, "arrow_bytes must not be negative"));
    }
    if (workload.rows > 0 && workload.arrow_bytes == 0) {
        return std::unexpected(make_error(
            Code::Invalid, "a non-empty table needs positive arrow_bytes"));
    }
    if (!(min_gain >= 0.0 && min_gain < 1.0)) {
        return std::unexpected(make_error(Code::Invalid, "min_gain must be in [0, 1)"));
    }
    const std::string_view topology_text = topology_name(topology);
    const std::string_view policy_text = policy_name(policy);
    const double mb = static_cast<double>(std::max<std::int64_t>(workload.arrow_bytes, 1)) /
                      1'000'000.0;
    const double baseline = model.predict("json", mb, topology_text, policy_text);

    Decision decision;
    decision.lane = "json";
    decision.predicted_ms = baseline;
    decision.predicted_bytes = workload.arrow_bytes;
    if (!workload.tabular) {
        decision.reason = "NON_TABULAR";
        return decision;
    }
    if (workload.rows <= 0) {
        decision.reason = "EMPTY";
        return decision;
    }

    std::vector<std::string> names;
    if (!allowed.empty()) {
        names = allowed;
    } else {
        for (const CostModel::Entry& entry : model.lanes()) {
            names.push_back(entry.name);
        }
    }
    std::vector<std::pair<std::string, double>> scores;
    std::vector<std::pair<std::string, std::string>> rejected;
    for (const std::string& name : names) {
        if (model.find(name) == nullptr) {
            rejected.emplace_back(name, "UNKNOWN_LANE");
            continue;
        }
        if (name == "shm" && !shm_ready) {
            rejected.emplace_back(name, "SHM_UNAVAILABLE");
            continue;
        }
        if (!platform_has(name)) {
            rejected.emplace_back(name, "NOT_AVAILABLE_ON_PLATFORM");
            continue;
        }
        scores.emplace_back(
            name, model.predict(name, mb, topology_text, policy_text));
    }
    if (scores.empty()) {
        decision.reason = "NO_CANDIDATE";
        decision.rejected = std::move(rejected);
        return decision;
    }

    std::string best_name;
    double best_value = 0.0;
    if (policy == Policy::Throughput) {
        double lowest = scores.front().second;
        for (const auto& score : scores) {
            lowest = std::min(lowest, score.second);
        }
        const double floor = lowest * 1.05;
        bool have = false;
        for (const auto& score : scores) {
            if (score.second > floor) continue;
            const auto bytes =
                model.bytes_for(score.first, workload.arrow_bytes);
            if (!have) {
                best_name = score.first;
                best_value = score.second;
                have = true;
                continue;
            }
            const auto best_bytes = model.bytes_for(best_name, workload.arrow_bytes);
            if (bytes < best_bytes || (bytes == best_bytes && score.second < best_value)) {
                best_name = score.first;
                best_value = score.second;
            }
        }
    } else {
        bool have = false;
        for (const auto& score : scores) {
            if (!have || score.second < best_value ||
                (score.second == best_value &&
                 tie_order(score.first) < tie_order(best_name))) {
                best_name = score.first;
                best_value = score.second;
                have = true;
            }
        }
    }

    bool has_json = false;
    for (const auto& score : scores) {
        if (score.first == "json") has_json = true;
    }
    if (best_name != "json" && has_json) {
        bool too_small = false;
        if (policy == Policy::Throughput) {
            const auto chosen = model.bytes_for(best_name, workload.arrow_bytes);
            const auto base = model.bytes_for("json", workload.arrow_bytes);
            too_small = static_cast<double>(chosen) >
                        static_cast<double>(base) * (1.0 - min_gain);
        } else {
            too_small = best_value > baseline * (1.0 - min_gain);
        }
        if (too_small) {
            decision.reason = "GAIN_TOO_SMALL";
            decision.scores = std::move(scores);
            decision.rejected = std::move(rejected);
            return decision;
        }
    }

    decision.lane = best_name;
    decision.reason = "BEST";
    decision.predicted_ms = best_value;
    decision.predicted_bytes = model.bytes_for(best_name, workload.arrow_bytes);
    decision.scores = std::move(scores);
    decision.rejected = std::move(rejected);
    return decision;
}

}  // namespace arrowlane
