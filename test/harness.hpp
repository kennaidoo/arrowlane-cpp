#pragma once

#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "arrowlane/error.hpp"

namespace altest {

using Fn = void (*)();

struct Case {
    const char* name;
    Fn fn;
};

struct Registrar {
    Registrar(const char* name, Fn fn);
};

std::vector<Case>& registry();
void fail(const char* file, int line, const std::string& message);
std::string to_text(const std::string& value);
std::string to_text(std::string_view value);
std::string to_text(const char* value);
std::string to_text(bool value);
std::string to_text(long long value);
std::string to_text(unsigned long long value);
std::string to_text(double value);
std::string to_text(std::byte value);

template <class T>
  requires std::is_integral_v<T>
std::string to_text(T value) {
    return std::to_string(value);
}

extern int checks;
extern int failures;

}  // namespace altest

#define AL_TEST(name)                                                        \
    static void name();                                                      \
    static ::altest::Registrar al_registrar_##name(#name, name);             \
    static void name()

#define AL_CHECK(cond)                                                       \
    do {                                                                     \
        ++::altest::checks;                                                  \
        if (!(cond)) ::altest::fail(__FILE__, __LINE__, "expected " #cond);   \
    } while (0)

#define AL_REQUIRE(cond)                                                     \
    do {                                                                     \
        ++::altest::checks;                                                  \
        if (!(cond)) {                                                       \
            ::altest::fail(__FILE__, __LINE__, "required " #cond);           \
            return;                                                          \
        }                                                                    \
    } while (0)

#define AL_CHECK_EQ(a, b)                                                    \
    do {                                                                     \
        ++::altest::checks;                                                  \
        const auto& al_a = (a);                                              \
        const auto& al_b = (b);                                              \
        if (!(al_a == al_b)) {                                               \
            ::altest::fail(__FILE__, __LINE__,                               \
                           std::string(#a " == " #b " (") +                  \
                               ::altest::to_text(al_a) + " vs " +            \
                               ::altest::to_text(al_b) + ")");               \
        }                                                                    \
    } while (0)

#define AL_CHECK_ERR(expr, expected)                                         \
    do {                                                                     \
        auto&& al_result = (expr);                                           \
        ++::altest::checks;                                                  \
        if (al_result.has_value()) {                                         \
            ::altest::fail(__FILE__, __LINE__,                               \
                           "expected error " #expected);                     \
        } else if (al_result.error().code != (expected)) {                   \
            ::altest::fail(__FILE__, __LINE__,                               \
                           std::string("expected ") +                        \
                               ::arrowlane::code_name(expected) + ", got " + \
                               ::arrowlane::code_name(al_result.error().code)); \
        }                                                                    \
    } while (0)
