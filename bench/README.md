# Benchmarks

One fixture, four implementations, one machine. The harness measures what it
can measure and says so when it cannot.

## Protocol

Every implementation prints one line per lane:

```text
<lane> <wire_bytes> <encode_ms> <decode_ms>
```

`arrowlane-cpp bench --machine` prints exactly that. The Rust and Zig binaries
and the Python driver already do, because this is the protocol the reference
repository uses, so the four numbers are comparable without translation.

Each implementation runs in its own container:

| Implementation | Image | Command |
|---|---|---|
| cpp | `arrowlane-bench-cpp:local` | `bench --rows N --repeats N --machine` |
| python | `arrowlane-bench-python:local` | `--rows N --repeats N` |
| rust | `arrowlane-bench-rust:local` | `bench --rows N --repeats N` |
| zig | `arrowlane-bench-zig:local` | `bench --rows N --repeats N` |

## Run it

```bash
python bench/compare.py --rows 200000 --repeats 7 --build
```

`--build` rebuilds the C++ image. Without it the existing image is used.
`--check` exits 1 when a win condition is missed.

Results land in `results/latest.md` and `results/latest.json`.

## The fixture

The fixture is defined by rules, not by a file: eight columns, the same values
in all four implementations. Equality is proven by the canonical digest check in
`interop/check_wire.py`, which compares the CRC32 of the value bytes against
pyarrow for every column. A shared file would add a copy step and prove nothing
that the digest does not already prove.

## What is not measured here

- Startup time: the Rust and Zig images are scratch images with no shell, so
  there is no way to time `--version` inside them.
- Peak RSS: same reason. `docs/performance.md` carries the C++ number, taken
  from the operating system inside `bench --json`.

Those rows are recorded as unproven, not as wins. A comparison that skips a
metric it cannot measure is honest. One that fills the gap with an estimate is
not.
