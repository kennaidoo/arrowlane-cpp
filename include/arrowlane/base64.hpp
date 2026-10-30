#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "arrowlane/error.hpp"

namespace arrowlane {

[[nodiscard]] std::string base64_encode(std::span<const std::byte> bytes);

[[nodiscard]] Result<std::vector<std::byte>> base64_decode(std::string_view text);

}  // namespace arrowlane
