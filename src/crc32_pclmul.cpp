#include "arrowlane/crc32.hpp"

#include "detail/crc32_x86.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define AL_HAVE_X86_64 1
#include <immintrin.h>
#include <smmintrin.h>
#include <tmmintrin.h>
#else
#define AL_HAVE_X86_64 0
#endif

namespace arrowlane {

#if AL_HAVE_X86_64

namespace {

// Constants from crc32fast 1.5.3 (MIT OR Apache-2.0), which follows the Intel
// white paper "Fast CRC computation for generic polynomials using PCLMULQDQ".
// For a fold by D bits the pair is
// (reflect(x^(D+32) mod P), reflect(x^(D-32) mod P)).
constexpr std::int64_t kK1 = 0x154442bd4LL;  // D = 512, low
constexpr std::int64_t kK2 = 0x1c6e41596LL;  // D = 512, high
constexpr std::int64_t kK3 = 0x1751997d0LL;  // D = 128, low
constexpr std::int64_t kK4 = 0x0ccaa009eLL;  // D = 128, high
constexpr std::int64_t kK5 = 0x163cd6124LL;  // 96 bits to 64 bits
constexpr std::int64_t kPolyX = 0x1DB710641LL;
constexpr std::int64_t kUPrime = 0x1F7011641LL;

#if defined(_MSC_VER)
#define AL_PCLMUL_TARGET
#else
#define AL_PCLMUL_TARGET __attribute__((target("pclmul,ssse3,sse4.1")))
#endif

AL_PCLMUL_TARGET __m128i load(const std::uint8_t* source) noexcept {
    return _mm_loadu_si128(reinterpret_cast<const __m128i*>(source));
}

// One fold: b xor (a low times key low) xor (a high times key high).
AL_PCLMUL_TARGET __m128i fold(__m128i a, __m128i b, __m128i keys) noexcept {
    return _mm_xor_si128(
        _mm_xor_si128(b, _mm_clmulepi64_si128(a, keys, 0x00)),
        _mm_clmulepi64_si128(a, keys, 0x11));
}

// Folds the remainder, reduces 128 bits to 64, then 64 to 32 with a Barrett
// step. The result is the complement of the second word of the final xor.
AL_PCLMUL_TARGET std::uint32_t reduce_to_crc(__m128i x, const std::uint8_t* data,
                                            std::size_t size) noexcept {
    const __m128i k3k4 = _mm_set_epi64x(kK4, kK3);
    while (size >= 16) {
        x = fold(x, load(data), k3k4);
        data += 16;
        size -= 16;
    }
    if (size > 0) {
        const __m128i sequence = _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                               12, 13, 14, 15);
        const __m128i shift = _mm_add_epi8(
            sequence, _mm_set1_epi8(static_cast<char>(static_cast<int>(size) - 16)));
        const __m128i keep =
            _mm_xor_si128(shift, _mm_set1_epi8(static_cast<char>(-128)));
        const __m128i overflow = _mm_shuffle_epi8(x, shift);
        x = _mm_shuffle_epi8(x, keep);
        alignas(16) std::uint8_t partial[16] = {};
        std::memcpy(partial, data, size);
        x = _mm_xor_si128(x, _mm_shuffle_epi8(load(partial), shift));
        x = fold(overflow, x, k3k4);
    }
    const __m128i low32 = _mm_set_epi32(0, 0, 0, -1);
    x = _mm_xor_si128(_mm_clmulepi64_si128(x, k3k4, 0x10), _mm_srli_si128(x, 8));
    x = _mm_xor_si128(
        _mm_clmulepi64_si128(_mm_and_si128(x, low32), _mm_set_epi64x(0, kK5), 0x00),
        _mm_srli_si128(x, 4));
    const __m128i pu = _mm_set_epi64x(kUPrime, kPolyX);
    const __m128i t1 = _mm_clmulepi64_si128(_mm_and_si128(x, low32), pu, 0x10);
    const __m128i t2 = _mm_clmulepi64_si128(_mm_and_si128(t1, low32), pu, 0x00);
    return ~static_cast<std::uint32_t>(_mm_extract_epi32(_mm_xor_si128(x, t2), 1));
}

AL_PCLMUL_TARGET std::uint32_t crc32_sse(const std::uint8_t* data,
                                         std::size_t size) noexcept {
    // Four accumulators while there are at least two 64 byte groups. The
    // starting value of zero is folded into the first block as its complement.
    if (size < 128) {
        __m128i x = _mm_xor_si128(load(data), _mm_cvtsi32_si128(-1));
        return reduce_to_crc(x, data + 16, size - 16);
    }
    __m128i x3 = load(data);
    __m128i x2 = load(data + 16);
    __m128i x1 = load(data + 32);
    __m128i x0 = load(data + 48);
    x3 = _mm_xor_si128(x3, _mm_cvtsi32_si128(-1));
    data += 64;
    size -= 64;
    const __m128i k1k2 = _mm_set_epi64x(kK2, kK1);
    while (size >= 64) {
        x3 = fold(x3, load(data), k1k2);
        x2 = fold(x2, load(data + 16), k1k2);
        x1 = fold(x1, load(data + 32), k1k2);
        x0 = fold(x0, load(data + 48), k1k2);
        data += 64;
        size -= 64;
    }
    const __m128i k3k4 = _mm_set_epi64x(kK4, kK3);
    __m128i x = fold(x3, x2, k3k4);
    x = fold(x, x1, k3k4);
    x = fold(x, x0, k3k4);
    return reduce_to_crc(x, data, size);
}

}  // namespace

namespace detail {

std::uint32_t crc32_finish_128(__m128i x, const std::uint8_t* data,
                               std::size_t size) noexcept {
    return reduce_to_crc(x, data, size);
}

}  // namespace detail

#endif  // AL_HAVE_X86_64

bool crc32_have_pclmul() noexcept {
#if !AL_HAVE_X86_64
    return false;
#elif defined(_MSC_VER)
    int registers[4] = {0, 0, 0, 0};
    __cpuid(registers, 1);
    const bool pclmul = (registers[2] & (1 << 1)) != 0;
    const bool ssse3 = (registers[2] & (1 << 9)) != 0;
    const bool sse41 = (registers[2] & (1 << 19)) != 0;
    return pclmul && ssse3 && sse41;
#else
    unsigned eax = 0;
    unsigned ebx = 0;
    unsigned ecx = 0;
    unsigned edx = 0;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) == 0) return false;
    return (ecx & bit_PCLMUL) != 0 && (ecx & bit_SSSE3) != 0 &&
           (ecx & bit_SSE4_1) != 0;
#endif
}

std::uint32_t crc32_pclmul(std::span<const std::byte> bytes) noexcept {
#if !AL_HAVE_X86_64
    return crc32_table(bytes);
#else
    if (bytes.size() < 16 || !crc32_have_pclmul()) return crc32_table(bytes);
    return crc32_sse(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
#endif
}

}  // namespace arrowlane
