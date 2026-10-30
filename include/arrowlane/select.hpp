#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "arrowlane/costmodel.hpp"
#include "arrowlane/error.hpp"

namespace arrowlane {

enum class Topology : std::uint8_t { SameHost = 0, SameNetwork = 1, WideArea = 2 };
enum class Policy : std::uint8_t { Latency = 0, Throughput = 1, Cpu = 2 };

[[nodiscard]] std::string_view topology_name(Topology topology) noexcept;
[[nodiscard]] std::string_view policy_name(Policy policy) noexcept;
[[nodiscard]] bool parse_topology(std::string_view text, Topology& out) noexcept;
[[nodiscard]] bool parse_policy(std::string_view text, Policy& out) noexcept;

struct Workload {
    std::int64_t rows = 0;
    std::int64_t arrow_bytes = 0;
    bool tabular = true;
};

struct Decision {
    std::string lane;
    std::string reason;
    double predicted_ms = 0.0;
    std::int64_t predicted_bytes = 0;
    std::vector<std::pair<std::string, double>> scores;
    std::vector<std::pair<std::string, std::string>> rejected;
};

[[nodiscard]] Result<Decision> select_plan(const Workload& workload, Topology topology,
                                           Policy policy, const CostModel& model,
                                           const std::vector<std::string>& allowed = {},
                                           bool shm_ready = false, double min_gain = 0.10);

}  // namespace arrowlane
