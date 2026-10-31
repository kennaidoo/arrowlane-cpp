#pragma once

#include <cstdint>

#include "arrowlane/column.hpp"

namespace arrowlane {

[[nodiscard]] RecordBatch make_fixture(std::int64_t rows);

// One field, three rows, two dictionary values. The smallest batch that proves
// the dictionary path end to end, and the fixture the CLI packs with
// --dictionary.
[[nodiscard]] RecordBatch make_dictionary_fixture();

}  // namespace arrowlane
