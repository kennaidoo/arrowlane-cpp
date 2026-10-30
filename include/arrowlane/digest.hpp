#pragma once

#include <cstdint>

#include "arrowlane/column.hpp"

namespace arrowlane {

[[nodiscard]] std::uint32_t column_digest(const Column& column, const Type& type);

}  // namespace arrowlane
