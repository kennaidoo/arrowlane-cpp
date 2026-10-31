// Eight stream CRC-32 over 256 bit carry-less multiplies.
//
// This is the only translation unit in the project compiled for AVX2. MSVC has
// no per function target attribute, so the file boundary is what keeps the
// 128 bit path usable on a CPU without AVX2: nothing here runs until
// crc32_have_vpclmul() has returned true.
//
// The structure follows crc32fast 1.5.3 (MIT OR Apache-2.0), which follows the
// Intel white paper "Fast CRC computation for generic polynomials using
// PCLMULQDQ": eight independent 128 bit streams, folded by 1024 bits per pass
// with the K_1024 constants, then collapsed in increasing byte offset with the
// 128 bit constants and finished by the shared tail.

#include "arrowlane/crc32.hpp"

#include "detail/crc32_x86.hpp"

#include <cstddef>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
// cpuid.h is an x86 header. Including it on aarch64 fails to compile.
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define AL_WIDE_X86 1
#include <immintrin.h>
#else
#define AL_WIDE_X86 0
#endif

namespace arrowlane {

#if AL_WIDE_X86

namespace {

#if defined(_MSC_VER)
#define AL_WIDE_TARGET
#else
#define AL_WIDE_TARGET \
    __attribute__((target("avx2,vpclmulqdq,pclmul,ssse3,sse4.1")))
#endif

// 128 bit fold constants, shared with the narrow path.
constexpr std::int64_t kK3 = 0x1751997d0LL;
constexpr std::int64_t kK4 = 0x0ccaa009eLL;
// Fold by 1024 bits: one pass advances each stream by 128 bytes.
constexpr std::int64_t kK1024Low = 0x1e88ef372LL;
constexpr std::int64_t kK1024High = 0x14a7fe880LL;

AL_WIDE_TARGET __m256i load256(const std::uint8_t* source) noexcept {
    return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(source));
}

// One fold over two 128 bit lanes at once.
AL_WIDE_TARGET __m256i fold256(__m256i a, __m256i b, __m256i keys) noexcept {
    return _mm256_xor_si256(
        _mm256_xor_si256(b, _mm256_clmulepi64_epi128(a, keys, 0x00)),
        _mm256_clmulepi64_epi128(a, keys, 0x11));
}

AL_WIDE_TARGET __m128i fold128(__m128i a, __m128i b, __m128i keys) noexcept {
    return _mm_xor_si128(
        _mm_xor_si128(b, _mm_clmulepi64_si128(a, keys, 0x00)),
        _mm_clmulepi64_si128(a, keys, 0x11));
}

// Requires size >= 256, which the caller checks.
AL_WIDE_TARGET std::uint32_t crc32_wide_kernel(const std::uint8_t* data,
                                            std::size_t size) noexcept {
    __m256i v0 = load256(data);
    __m256i v1 = load256(data + 32);
    __m256i v2 = load256(data + 64);
    __m256i v3 = load256(data + 96);
    // The initial value of 0xFFFFFFFF complements the first four bytes.
    v0 = _mm256_xor_si256(v0, _mm256_castsi128_si256(_mm_cvtsi32_si128(-1)));
    data += 128;
    size -= 128;

    const __m256i k = _mm256_set_epi64x(kK1024High, kK1024Low, kK1024High,
                                        kK1024Low);
    while (size >= 128) {
        v0 = fold256(v0, load256(data), k);
        v1 = fold256(v1, load256(data + 32), k);
        v2 = fold256(v2, load256(data + 64), k);
        v3 = fold256(v3, load256(data + 96), k);
        data += 128;
        size -= 128;
    }

    // Collapse the eight streams in increasing byte offset.
    const __m128i k3k4 = _mm_set_epi64x(kK4, kK3);
    __m128i x = _mm256_castsi256_si128(v0);
    x = fold128(x, _mm256_extracti128_si256(v0, 1), k3k4);
    x = fold128(x, _mm256_castsi256_si128(v1), k3k4);
    x = fold128(x, _mm256_extracti128_si256(v1, 1), k3k4);
    x = fold128(x, _mm256_castsi256_si128(v2), k3k4);
    x = fold128(x, _mm256_extracti128_si256(v2, 1), k3k4);
    x = fold128(x, _mm256_castsi256_si128(v3), k3k4);
    x = fold128(x, _mm256_extracti128_si256(v3, 1), k3k4);
    return detail::crc32_finish_128(x, data, size);
}

}  // namespace

#endif  // AL_WIDE_X86

bool crc32_have_vpclmul() noexcept {
#if !AL_WIDE_X86
    return false;
#else
#if defined(_MSC_VER)
    int registers[4] = {0, 0, 0, 0};
    __cpuidex(registers, 7, 0);
    const bool avx2 = (registers[1] & (1 << 5)) != 0;
    const bool vpclmul = (registers[2] & (1 << 10)) != 0;
    if (!avx2 || !vpclmul) return false;
    const unsigned long long state = _xgetbv(0);
#else
    unsigned eax = 0;
    unsigned ebx = 0;
    unsigned ecx = 0;
    unsigned edx = 0;
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) == 0) return false;
    const bool avx2 = (ebx & (1u << 5)) != 0;
    const bool vpclmul = (ecx & (1u << 10)) != 0;
    if (!avx2 || !vpclmul) return false;
    unsigned low = 0;
    unsigned high = 0;
    __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
    const unsigned long long state =
        (static_cast<unsigned long long>(high) << 32) | low;
#endif
    // Bit 1 is the SSE state, bit 2 is the YMM state. Without both, the
    // operating system does not preserve the registers this file uses.
    return (state & 0x6ULL) == 0x6ULL;
#endif
}

std::uint32_t crc32_wide(std::span<const std::byte> bytes) noexcept {
#if !AL_WIDE_X86
    return crc32_table(bytes);
#else
    if (bytes.size() < 256 || !crc32_have_vpclmul()) return crc32_pclmul(bytes);
    return crc32_wide_kernel(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                          bytes.size());
#endif
}

}  // namespace arrowlane
