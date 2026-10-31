#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "arrowlane/error.hpp"

namespace arrowlane {

// Appends the base64 text to out. No allocation of its own beyond out's growth.
void base64_append(std::string& out, std::span<const std::byte> bytes);

[[nodiscard]] std::string base64_encode(std::span<const std::byte> bytes);

[[nodiscard]] Result<std::vector<std::byte>> base64_decode(std::string_view text);

}  // namespace arrowlane
