#include "arrowlane/error.hpp"

#include <cstdio>
#include <string>

int main() {
    const auto error = arrowlane::make_error(arrowlane::Code::BadMagic, "test");
    if (error.to_string() != "BadMagic: test") {
        std::printf("FAIL %s\n", error.to_string().c_str());
        return 1;
    }
    std::printf("ok 1 test, 1 check\n");
    return 0;
}
