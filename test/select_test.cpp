#include "harness.hpp"

#include "arrowlane/costmodel.hpp"
#include "arrowlane/select.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace {

bool near(double left, double right, double tolerance = 1e-4) {
    return std::fabs(left - right) <= tolerance;
}

}  // namespace

AL_TEST(cost_model_builtin_parses) {
    auto model = arrowlane::CostModel::builtin();
    AL_REQUIRE(model.has_value());
    AL_CHECK_EQ(model->lanes().size(), std::size_t{4});
    const auto* json = model->find("json");
    AL_REQUIRE(json != nullptr);
    AL_CHECK(near(json->ratio, 2.615945, 1e-12));
    AL_CHECK(near(json->fixed_ms, 0.002, 1e-12));
    AL_CHECK(near(model->wire_us_per_mb("same_network"), 800.0, 1e-12));
    AL_CHECK(model->find("shm") != nullptr);
}

AL_TEST(cost_model_rejects_a_wrong_version) {
    const std::string text = R"({"schema_version":2,"provenance":{"a":1},)"
                             R"("wire_us_per_mb":{"same_host":1,"same_network":1,"wide_area":1},)"
                             R"("lanes":{"json":{"fixed_ms":0,"enc_ms_per_mb":0,"dec_ms_per_mb":0,"ratio":1}}})";
    AL_CHECK_ERR(arrowlane::CostModel::parse(text), arrowlane::Code::Config);
}

AL_TEST(cost_model_rejects_a_negative_number) {
    const std::string text = R"({"schema_version":1,"provenance":{"a":1},)"
                             R"("wire_us_per_mb":{"same_host":-1,"same_network":1,"wide_area":1},)"
                             R"("lanes":{"json":{"fixed_ms":0,"enc_ms_per_mb":0,"dec_ms_per_mb":0,"ratio":1}}})";
    AL_CHECK_ERR(arrowlane::CostModel::parse(text), arrowlane::Code::Config);
}

AL_TEST(cost_model_rejects_an_unknown_lane_key) {
    const std::string text = R"({"schema_version":1,"provenance":{"a":1},)"
                             R"("wire_us_per_mb":{"same_host":1,"same_network":1,"wide_area":1},)"
                             R"("lanes":{"json":{"fixed_ms":0,"enc_ms_per_mb":0,"dec_ms_per_mb":0,"ratio":1,"extra":2}}})";
    AL_CHECK_ERR(arrowlane::CostModel::parse(text), arrowlane::Code::Config);
}

AL_TEST(cost_model_predicts_the_json_baseline) {
    auto model = arrowlane::CostModel::builtin();
    AL_REQUIRE(model.has_value());
    const double expected = 0.002 + (197.329541 + 152.264359) + 0.8 * 2.615945;
    AL_CHECK(near(model->predict("json", 1.0, "same_network", "latency"), expected,
                  1e-12));
    const double cpu = 0.002 + (197.329541 + 152.264359);
    AL_CHECK(near(model->predict("json", 1.0, "same_network", "cpu"), cpu, 1e-12));
}

AL_TEST(select_matches_oracle_one) {
    auto model = arrowlane::CostModel::builtin();
    AL_REQUIRE(model.has_value());
    arrowlane::Workload workload;
    workload.rows = 200000;
    workload.arrow_bytes = 13380000;
    auto decision = arrowlane::select_plan(workload, arrowlane::Topology::SameNetwork,
                                           arrowlane::Policy::Latency, *model);
    AL_REQUIRE(decision.has_value());
    AL_CHECK_EQ(decision->lane, std::string("ipc"));
    AL_CHECK_EQ(decision->reason, std::string("BEST"));
    AL_CHECK(near(decision->predicted_ms, 14.8256));
    AL_CHECK_EQ(decision->predicted_bytes, std::int64_t{13380454});
}

AL_TEST(select_matches_oracle_two) {
    auto model = arrowlane::CostModel::builtin();
    AL_REQUIRE(model.has_value());
    arrowlane::Workload workload;
    workload.rows = 500;
    workload.arrow_bytes = 64;
    auto decision = arrowlane::select_plan(workload, arrowlane::Topology::WideArea,
                                           arrowlane::Policy::Cpu, *model);
    AL_REQUIRE(decision.has_value());
    AL_CHECK_EQ(decision->lane, std::string("json"));
    AL_CHECK_EQ(decision->reason, std::string("BEST"));
    AL_CHECK(near(decision->predicted_ms, 0.0244));
    AL_CHECK_EQ(decision->predicted_bytes, std::int64_t{167});
}

AL_TEST(select_matches_oracle_three) {
    auto model = arrowlane::CostModel::builtin();
    AL_REQUIRE(model.has_value());
    arrowlane::Workload workload;
    workload.rows = 0;
    workload.arrow_bytes = 0;
    auto decision = arrowlane::select_plan(workload, arrowlane::Topology::SameHost,
                                           arrowlane::Policy::Latency, *model);
    AL_REQUIRE(decision.has_value());
    AL_CHECK_EQ(decision->lane, std::string("json"));
    AL_CHECK_EQ(decision->reason, std::string("EMPTY"));
    AL_CHECK(near(decision->predicted_ms, 0.0024));
    AL_CHECK_EQ(decision->predicted_bytes, std::int64_t{0});
    AL_CHECK(decision->scores.empty());
    AL_CHECK(decision->rejected.empty());
}

AL_TEST(select_matches_oracle_four) {
    auto model = arrowlane::CostModel::builtin();
    AL_REQUIRE(model.has_value());
    arrowlane::Workload workload;
    workload.rows = 1000;
    workload.arrow_bytes = 1000;
    auto decision = arrowlane::select_plan(workload, arrowlane::Topology::SameHost,
                                           arrowlane::Policy::Throughput, *model);
    AL_REQUIRE(decision.has_value());
    AL_CHECK_EQ(decision->lane, std::string("ipc"));
    AL_CHECK_EQ(decision->reason, std::string("BEST"));
    AL_CHECK(near(decision->predicted_ms, 0.0606));
    AL_CHECK_EQ(decision->predicted_bytes, std::int64_t{1000});
}

AL_TEST(select_handles_allow_lists_and_rejections) {
    auto model = arrowlane::CostModel::builtin();
    AL_REQUIRE(model.has_value());
    arrowlane::Workload workload;
    workload.rows = 1000;
    workload.arrow_bytes = 1000;

    const std::vector<std::string> only_ipc{"ipc"};
    auto decision = arrowlane::select_plan(workload, arrowlane::Topology::SameHost,
                                           arrowlane::Policy::Latency, *model, only_ipc);
    AL_REQUIRE(decision.has_value());
    AL_CHECK_EQ(decision->lane, std::string("ipc"));
    AL_CHECK_EQ(decision->reason, std::string("BEST"));
    AL_CHECK_EQ(decision->scores.size(), std::size_t{1});

    const std::vector<std::string> unknown{"nope"};
    auto missing = arrowlane::select_plan(workload, arrowlane::Topology::SameHost,
                                          arrowlane::Policy::Latency, *model, unknown);
    AL_REQUIRE(missing.has_value());
    AL_CHECK_EQ(missing->lane, std::string("json"));
    AL_CHECK_EQ(missing->reason, std::string("NO_CANDIDATE"));
    AL_REQUIRE(missing->rejected.size() == 1);
    AL_CHECK_EQ(missing->rejected[0].second, std::string("UNKNOWN_LANE"));

    auto with_shm = arrowlane::select_plan(workload, arrowlane::Topology::SameHost,
                                           arrowlane::Policy::Latency, *model, {}, false);
    AL_REQUIRE(with_shm.has_value());
    bool shm_rejected = false;
    for (const auto& entry : with_shm->rejected) {
        if (entry.first == "shm" && entry.second == "SHM_UNAVAILABLE") shm_rejected = true;
    }
    AL_CHECK(shm_rejected);
}
