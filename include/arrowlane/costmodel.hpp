#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "arrowlane/error.hpp"

namespace arrowlane {

struct LaneCost {
    double fixed_ms = 0.0;
    double enc_ms_per_mb = 0.0;
    double dec_ms_per_mb = 0.0;
    double ratio = 0.0;
};

// The shipped model, generated from src/costmodel.json at configure time.
[[nodiscard]] std::string_view builtin_cost_model_json() noexcept;

class CostModel {
public:
    struct Entry {
        std::string name;
        LaneCost cost;
    };

    [[nodiscard]] static Result<CostModel> parse(std::string_view json);
    [[nodiscard]] static Result<CostModel> from_file(const std::string& path);
    [[nodiscard]] static Result<CostModel> builtin();

    [[nodiscard]] double predict(std::string_view lane, double mb,
                                 std::string_view topology,
                                 std::string_view policy) const;
    [[nodiscard]] std::int64_t bytes_for(std::string_view lane,
                                         std::int64_t arrow_bytes) const;
    [[nodiscard]] double wire_us_per_mb(std::string_view topology) const;
    [[nodiscard]] const std::vector<Entry>& lanes() const noexcept { return lanes_; }
    [[nodiscard]] const LaneCost* find(std::string_view lane) const noexcept;

private:
    int schema_version_ = 0;
    std::vector<Entry> lanes_;
    double same_host_ = 0.0;
    double same_network_ = 0.0;
    double wide_area_ = 0.0;
};

}  // namespace arrowlane
