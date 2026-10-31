#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>

namespace arrowlane::detail {

// Tail of a carry-less multiply CRC: x holds the folded state, data and size
// hold the bytes that are left. Defined in src/crc32_pclmul.cpp, which is
// compiled without AVX2, so a caller that is compiled with AVX2 can hand work
// back to code that runs on any CPU with PCLMULQDQ.
std::uint32_t crc32_finish_128(__m128i x, const std::uint8_t* data,
                               std::size_t size) noexcept;

}  // namespace arrowlane::detail

#endif
