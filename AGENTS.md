# AGENTS.md

Instructions for any agent working in this repository. Read this before the first
command. The rules here are not style preferences: they are what keeps the
numbers in `docs/performance.md` and the bytes on the wire trustworthy.

## What this project is

A pure C++ implementation of the arrowlane wire format, byte compatible with the
Python reference at `../arrowlane`. No Arrow C++ dependency, no Python at
runtime, no FFI. The only third party code is zstd, fetched into `vendor/zstd`.

## Build and test

| Action | Command | Expected |
|---|---|---|
| Debug build | `scripts\build.cmd` | ends with no error, tree in `build/` |
| Release build | `scripts\build.cmd Release` | tree in `build-release/` |
| Whole gate | `scripts\gate.cmd` | `RESULT: PASS` |
| One test | `.\build\<name>_test.exe` | `ok N tests, M checks` |
| One test, verbose | set `AL_VERBOSE=1` first | prints every check |
| Container suite | `tools\run-tests.cmd` | `100% tests passed out of 24` plus seven interop checks |
| Benchmark | `python bench\compare.py --rows 200000 --repeats 7 --rounds 3 --check` | five verdict lines, exit 0 |

On Windows the shell is git-bash, so native tool wrappers need
`MSYS2_ARG_CONV_EXCL='*' cmd.exe /c '<absolute path>.cmd'`. Never call `cl`,
`cmake` or `ninja` directly: `scripts\env.cmd` sets them up.

Long builds and the benchmark belong in a container or a background job. Do not
run them in the foreground where they interrupt the user.

## Rules

1. **Evidence over description.** Every claim in a report names the command that
   produced it and the line it printed. A number without a command is a guess.
2. **Never invent a measurement.** If a number cannot be taken, write
   "unmeasured" and say why. Fabricating a plausible figure is the worst failure
   this project can have, because the whole point is that the numbers are real.
3. **No wire change without a test.** The golden payload is 606 bytes for the
   3-row fixture, the ipc wire is 12670881 bytes, and
   `interop/check_wire.py` compares digests against pyarrow. If a change alters
   bytes, stop and say so.
4. **One commit per task.** Message form: `type(scope): imperative summary`.
   Types used here: `feat`, `fix`, `perf`, `test`, `docs`, `refactor`, `build`.
   Commit with `git -c core.autocrlf=false commit` on this machine.
5. **TDD for behaviour changes.** Write the failing test, run it, watch it fail
   for the stated reason, then implement. A test that passes before the change
   proves nothing.
6. **A change that does not move its number is reverted and recorded.** Dead
   optimisation is worse than no optimisation, because it costs review time.
7. **Debug numbers are never performance numbers.** A debug build measured 30
   times slower than release here. `describe` prints `build_type`; check it.
8. **Ratios only inside one run.** The reference implementations move by up to a
   factor of two between runs. Use `--rounds 3` and compare medians with ranges.
   If ranges overlap, the answer is `UNDETERMINED`, not a verdict.
9. **Zero em dashes.** Not in code, comments, docs, or commit messages. Use a
   comma, a colon, or a full stop.
10. **TigerStyle.** Bounded loops, explicit limits, simple control flow, visible
    invariants, fail fast on bad input. Every loop over untrusted data has a cap.
11. **No `__int128`.** MSVC lacks it. Decimal128 uses the four limb algorithms in
    `src/detail/decimal.hpp`.
12. **Portability.** C++23 baseline compiled as `/std:c++latest`. Anything newer
    needs a feature macro and a portable fallback, and its probe result recorded
    in `docs/feature-matrix.md`.
13. **One translation unit is compiled for AVX2**: `src/crc32_vpclmul.cpp`. Do not
    move code into it that runs before `crc32_have_vpclmul()` returns true.
14. **Version 0.1.0 until a published release.** Do not bump the version to
    signal completeness; the wire format version is separate and is 1.

## Layout

| Path | Holds |
|---|---|
| `include/arrowlane/` | public headers, the only surface a user sees |
| `src/` | implementation, `src/detail/` for internals |
| `test/` | one binary per unit, `test/harness.cpp` provides `AL_TEST` |
| `interop/` | Python checks against the reference, all need `ARROWLANE_SRC` |
| `bench/` | four way harness, one Dockerfile per implementation |
| `tools/` | container definition, runner, zstd fetch, feature probes |
| `docs/` | decisions, performance record, feature matrix, AI manifest |

## When you are stuck

Ask. The user decides repository visibility, destructive actions, external
publication, and anything that changes the wire. Do not guess on those.

## Cross-repository check

The Python package and the C++ port must agree on bytes. The C++ test writes a
dictionary stream, and the Python test reads it:

    # in arrowlane-cpp, build and test everything
    scripts/gate.cmd

    # in arrowlane, point at the fixture the C++ test wrote
    ARROWLANE_CPP_DICT=<path to arrowlane-cpp>/build/dict_cpp.arrows \
      uv run --no-sync pytest tests/test_dictionary_interop.py -q

Expected: `1 passed`, not `1 skipped`. A skip means the environment variable did
not reach the test, so the check did not run.
