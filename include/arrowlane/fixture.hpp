#pragma once

#include <cstdint>

#include "arrowlane/column.hpp"

namespace arrowlane {

[[nodiscard]] RecordBatch make_fixture(std::int64_t rows);

}  // namespace arrowlane
