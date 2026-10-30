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

## Rules

1. Re-run the probe at every toolchain update. A feature that starts to pass
   gets a task. It does not enter `src` on the same day.
2. Every adoption names the feature test macro and the cppreference page in a
   comment, and has a test that fails if the feature is absent.
3. A feature that is absent gets a portable fallback, not `#error`.
4. No experimental type crosses a public header. Wrap it in `src/detail/`.
