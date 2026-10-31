// Does this toolchain emit 256 bit VPCLMULQDQ for this CPU, and does the CPU
// run it? A compile failure is a result, not an error: it decides which
// checksum kernel the project can use.
//
// Build on Windows:
//   cl /nologo /std:c++latest /EHsc /arch:AVX2 /Fe:probe.exe vpclmul.cpp
// Build with GCC:
//   g++ -O2 -std=c++23 -mavx2 -mvpclmulqdq vpclmul.cpp -o probe

#include <cstdio>
#include <immintrin.h>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

int main() {
    int registers[4] = {0, 0, 0, 0};
#if defined(_MSC_VER)
    __cpuidex(registers, 7, 0);
#else
    __cpuid_count(7, 0, registers[0], registers[1], registers[2], registers[3]);
#endif
    const bool avx2 = (registers[1] & (1 << 5)) != 0;
    const bool vpclmul = (registers[2] & (1 << 10)) != 0;
    std::printf("avx2=%d vpclmulqdq=%d\n", avx2 ? 1 : 0, vpclmul ? 1 : 0);
    if (!avx2 || !vpclmul) {
        std::printf("PROBE: instructions absent, use the 128 bit path\n");
        return 0;
    }

    const __m256i a = _mm256_set_epi64x(1, 2, 3, 4);
    const __m256i b = _mm256_set_epi64x(5, 6, 7, 8);
    const __m256i product = _mm256_clmulepi64_epi128(a, b, 0x00);
    const __m256i mixed = _mm256_xor_si256(product, a);
    alignas(32) long long out[4] = {0, 0, 0, 0};
    _mm256_store_si256(reinterpret_cast<__m256i*>(out), mixed);
    std::printf("PROBE: ran, result=%lld %lld %lld %lld\n", out[0], out[1], out[2],
                out[3]);
    return 0;
}
