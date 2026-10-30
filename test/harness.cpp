#include "harness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

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

int main(int argc, char** argv) {
    const std::string_view filter = argc > 1 ? argv[1] : "";
#if defined(_MSC_VER)
    char* verbose_value = nullptr;
    std::size_t verbose_size = 0;
    const bool verbose =
        _dupenv_s(&verbose_value, &verbose_size, "AL_VERBOSE") == 0 &&
        verbose_value != nullptr;
    std::free(verbose_value);
#else
    const bool verbose = std::getenv("AL_VERBOSE") != nullptr;
#endif
    int failed_tests = 0;
    std::size_t ran = 0;
    for (const auto& test : altest::registry()) {
        if (!filter.empty() &&
            std::string_view(test.name).find(filter) == std::string_view::npos) {
            continue;
        }
        ++ran;
        if (verbose) std::fprintf(stderr, "run %s\n", test.name);
        const int before = altest::failures;
        test.fn();
        if (altest::failures != before) {
            ++failed_tests;
            std::printf("not ok %s\n", test.name);
        }
    }
    if (failed_tests != 0) {
        std::printf("%d of %zu tests failed, %d checks failed\n", failed_tests, ran,
                    altest::failures);
        return 1;
    }
    std::printf("ok %zu tests, %d checks\n", ran, altest::checks);
    return 0;
}
