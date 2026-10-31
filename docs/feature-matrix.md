# Feature matrix

Every candidate feature is compiled before it enters `src`. One translation unit
per candidate, so a missing header fails one file and not the run.

## MSVC

| Item | Value |
|---|---|
| Compiler | MSVC 14.51.36231 (`cl` 19.51.36260.0) |
| Standard mode | `/std:c++latest`, `__cplusplus` = 202400 |
| Date | 2026-10-30 |
| Command | `tools\feature_probe\run.cmd` |

| Feature | Spelling | Result | Use here |
|---|---|---|---|
| `std::generator` | `<generator>`, `co_yield` | PASS | lazy chunk producer wrapper |
| `std::start_lifetime_as` | `<memory>` | PASS | typed views over column bytes |
| `std::start_lifetime_as_array` | `<memory>` | PASS | typed array views |
| `std::start_lifetime_as_array`, const source | `<memory>`, `const std::byte*` | PASS | view path over a read only payload |
| `std::execution` header | `<execution>` | PASS, header only | not used in v0.1 |
| Parallelism TS v2 `simd` | `<experimental/simd>` | FAIL, header absent | AVX2 intrinsics in P10.4 |
| LFTS v3 `scope_exit` | `<experimental/scope>` | FAIL | own `ScopeExit` in `src/detail/scope.hpp` |
| LFTS v3 `observer_ptr` | `<experimental/memory>` | FAIL | not needed |
| Concurrency TS v2 hazard pointer | `<experimental/hazard_pointer>` | FAIL | not needed, single threaded |
| Concurrency TS v2 RCU | `<experimental/rcu>` | FAIL | not needed, single threaded |
| `std::submdspan` | `<mdspan>` | FAIL, `mdspan` itself PASS | not used |
| `std::function_ref` | `<functional>` | FAIL | plain function pointers |
| `std::copyable_function` | `<functional>` | FAIL | `std::function` only in the CLI |
| `std::is_within_lifetime` | `<type_traits>` | FAIL | not used |
| Contracts `[[pre:]]` | language | FAIL | `Result` plus asserts |
| C23 `<stdckdint.h>` | C header | FAIL in C++ mode | manual overflow checks |

Adopted from the passing set: `std::expected`, `std::span`, `std::generator`,
`std::start_lifetime_as_array`, `std::to_chars`, `std::from_chars`,
`std::byteswap`, `std::bit_cast`.

Absent, with a fallback: `simd` uses AVX2 intrinsics, `scope_exit` uses a local
`ScopeExit`, contracts use `Result` and asserts, and C23 checked arithmetic is
written by hand.

## GCC

| Item | Value |
|---|---|
| Compiler | g++ (Debian 12.2.0-14+deb12u1) 12.2.0 |
| Image | `rust:1.99.1-bookworm`, `sha256:9181b56c2a21ab1d5e73cffe04fc762426d30aa7fac3c91141dd033f49a7fc8b` |
| Standard | `-std=c++23` |
| Date | 2026-10-30 |
| Command | `bash tools/feature_probe/run_docker.sh` |

| Feature | Result |
|---|---|
| `<execution>` header | PASS |
| `std::experimental::observer_ptr` | PASS |
| `std::experimental::simd` | PASS |
| `<stdckdint.h>` | FAIL |
| Contracts `[[pre:]]` | FAIL |
| `std::copyable_function` | FAIL |
| `std::function_ref` | FAIL |
| `std::generator` | FAIL |
| `<experimental/hazard_pointer>` | FAIL |
| `<experimental/rcu>` | FAIL |
| `<experimental/scope>` | FAIL |
| `std::start_lifetime_as` | FAIL |
| `std::start_lifetime_as_array` | FAIL |
| `std::start_lifetime_as_array`, const source | FAIL |
| `std::submdspan` | FAIL |
| `std::is_within_lifetime` | FAIL |

Interpretation:

- GCC 12.2 is older than the MSVC 14.51 library. `generator` and
  `start_lifetime_as` fail because GCC 12 predates the C++23 `<generator>` and
  the C++26 lifetime facilities. The portable fallbacks stay.
- `simd` and `observer_ptr` pass on GCC only. They do not enter `src`, because
  the MSVC probe fails and the AVX2 intrinsic path is already chosen. A future
  GCC only path needs a macro and its own tests.
- The product code keeps the MSVC passing set and the portable fallbacks. No
  source file depends on a GCC only feature.

## CPU instruction probes

These are not language features. They decide which kernel can run, so the answer
is recorded with the command that produced it.

| Intrinsic | MSVC 14.51 | GCC 16.2.0 | Command |
|---|---|---|---|
| `_mm256_clmulepi64_epi128` (256 bit VPCLMULQDQ) | runs | runs | `scripts\probe-vpclmul.cmd`, and the same source with `g++ -O2 -std=c++23 -mavx2 -mvpclmulqdq` |

Both toolchains print the same line on the Ryzen 7 5800X3D:

```text
avx2=1 vpclmulqdq=1
PROBE: ran, result=36 3 14 1
```

Two things follow. MSVC needs `/arch:AVX2` to emit the intrinsic, and it has no
per function equivalent, so the kernel gets its own translation unit
(`src/crc32_vpclmul.cpp`) and the rest of the project stays at baseline. GCC
takes the target attribute instead. The CPU reports `vpclmulqdq` with no
`avx512f`, which is Zen 3: the 256 bit form exists without AVX-512.

## Rules

1. Re-run the probe at every toolchain update. A feature that starts to pass
   gets a task. It does not enter `src` on the same day.
2. Every adoption names the feature test macro and the cppreference page in a
   comment, and has a test that fails if the feature is absent.
3. A feature that is absent gets a portable fallback, not `#error`.
4. No experimental type crosses a public header. Wrap it in `src/detail/`.

## Instruction level inspection, 2026-10-31

Disassembled with `dumpbin /disasm` on the Release objects and on the linked
executable, and with `objdump -d` on an aarch64 object built by the same command
the arm64 CI job uses.

### x86-64

| Unit | Contents | First instruction |
|---|---|---|
| `crc32_vpclmul.cpp` object | 26 `vpclmulqdq`, 27 `vpxor`, 1 `vzeroupper` | `C4 E3 55 44 C4 00  vpclmulqdq ymm0,ymm5,ymm4,0` |
| `crc32_pclmul.cpp` object | 24 `pclmulqdq`, **0** `vpclmulqdq` | `66 0F 3A 44 CA 00  pclmulqdq xmm1,xmm2,0` |
| linked `arrowlane-cpp.exe` | 22 `vpclmulqdq`, 17 `pclmulqdq`, 34 `vzeroupper` | |

Readings:

- `C4` is the three byte VEX prefix, `E3` sets the 0F3A opcode map and W=0, `55`
  encodes 256 bit vectors with `ymm5` as the second source, `44` is the PCLMULQDQ
  opcode, `C4` is the ModRM byte and the trailing `00` selects the low 64 bit
  halves. The `11h` form selects the high halves.
- The baseline unit contains no VEX prefix at all, so the file split does what it
  claims: that object is safe on a CPU without AVX2.
- The 256 byte threshold is visible in the kernel itself: `49 81 F9 00 01 00`
  (`cmp r9,100h`) followed by `72 50` (`jb`).
- The guard is the correct AVX2 sequence: `B8 07 00 00 00` (`mov eax,7`),
  `0F A2` (`cpuid`), `F6 C3 20` (`test bl,20h`, the AVX2 bit), `0F 01 D0`
  (`xgetbv`).
- The object holds 26 `vpclmulqdq` across `crc32_wide_sse` (22) and the two fold
  helpers (2 each); the linked executable holds 22, the difference being link time
  optimisation. The functional proof stays the test suite, which compares the wide
  kernel against the table on 2000 buffers.

### arm64

```
9ac14c00  crc32x  w0, w0, x1
1ac14000  crc32b  w0, w0, w1
```

ACLE exposes `__crc32d` as CRC32X, so the eight byte loop is the hardware
instruction and the tail loop is CRC32B. Without `-march=armv8-a+crc` GCC refuses
to inline either and the file does not compile, which is what the first arm64 CI
run reported.
