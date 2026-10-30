#include "harness.hpp"

#include <algorithm>
#include <cmath>

namespace altest {

namespace {
std::vector<Case>& cases() {
    static std::vector<Case> list;
    return list;
}
}  // namespace

std::vector<Case>& registry() { return cases(); }

Registrar::Registrar(const char* name, Fn fn) { registry().push_back(Case{name, fn}); }

int checks = 0;
int failures = 0;

void fail(const char* file, int line, const std::string& message) {
    ++failures;
    std::printf("FAIL %s:%d: %s\n", file, line, message.c_str());
}

std::string to_text(const std::string& value) { return value; }
std::string to_text(std::string_view value) { return std::string(value); }
std::string to_text(const char* value) { return value == nullptr ? "<null>" : value; }
std::string to_text(bool value) { return value ? "true" : "false"; }
std::string to_text(long long value) { return std::to_string(value); }
std::string to_text(unsigned long long value) { return std::to_string(value); }
std::string to_text(std::byte value) {
    return std::to_string(std::to_integer<unsigned>(value));
}
std::string to_text(double value) {
    std::ostringstream out;
    out.precision(17);
    out << value;
    return out.str();
}

}  // namespace altest

int main() {
    int failed_tests = 0;
    for (const auto& test : altest::registry()) {
        const int before = altest::failures;
        test.fn();
        if (altest::failures != before) {
            ++failed_tests;
            std::printf("not ok %s\n", test.name);
        }
    }
    if (failed_tests != 0) {
        std::printf("%d of %zu tests failed, %d checks failed\n", failed_tests,
                    altest::registry().size(), altest::failures);
        return 1;
    }
    std::printf("ok %zu tests, %d checks\n", altest::registry().size(), altest::checks);
    return 0;
}
