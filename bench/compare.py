"""Compare the four implementations on one fixture and one machine.

Every implementation runs in its own container and prints one line per lane:

    <lane> <wire_bytes> <encode_ms> <decode_ms>

The C++ binary does that with `bench --machine`. The Rust and Zig binaries and
the Python driver already print it, because this is the protocol the reference
repository uses.

Usage:
    python bench/compare.py --rows 200000 --repeats 7
    python bench/compare.py --rows 200000 --repeats 7 --check
"""

from __future__ import annotations

import argparse
import datetime
import json
import platform
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
RESULTS = HERE / "results"
LANES = ("json", "ipc", "ipc-zstd")
IMPLS = ("cpp", "python", "rust", "zig")
IMAGES = {
    "cpp": "arrowlane-bench-cpp:local",
    "python": "arrowlane-bench-python:local",
    "rust": "arrowlane-bench-rust:local",
    "zig": "arrowlane-bench-zig:local",
}
# Everything except the Python image takes a subcommand.
PREFIX = {"cpp": ["bench"], "python": [], "rust": ["bench"], "zig": ["bench"]}
METHOD = {
    "cpp": "in process, encode and frame, mean of the repeats",
    "python": "in process, pack and unpack, mean of the repeats",
    "rust": "in process, mean of the repeats",
    "zig": "in process, mean of the repeats",
}

# Field positions into the (wire, enc, dec) tuple.
WIRE, ENCODE, DECODE = 0, 1, 2

# Ratios come from the plan's win table: the C++ number must be at most ratio
# times the reference number.
CONDITIONS = (
    {"name": "ipc_encode_ms", "reference": "rust", "ratio": 0.9, "lane": "ipc",
     "field": ENCODE},
    {"name": "ipc_decode_ms", "reference": "rust", "ratio": 0.8, "lane": "ipc",
     "field": DECODE},
    {"name": "json_encode_ms", "reference": "python", "ratio": 1.1, "lane": "json",
     "field": ENCODE},
    {"name": "json_decode_ms", "reference": "rust", "ratio": 0.6, "lane": "json",
     "field": DECODE},
    {"name": "ipc_wire_bytes", "reference": "python", "ratio": 1.02, "lane": "ipc",
     "field": WIRE},
)

DOCKER = shutil.which("docker")


def run(argv: list[str]) -> str:
    result = subprocess.run(argv, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(
            f"{' '.join(argv)} failed with {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result.stdout


def parse(text: str) -> dict[str, tuple[int, float, float]]:
    found: dict[str, tuple[int, float, float]] = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) != 4 or parts[0] not in LANES:
            continue
        try:
            found[parts[0]] = (int(parts[1]), float(parts[2]), float(parts[3]))
        except ValueError as error:
            raise SystemExit(f"cannot parse {line!r}") from error
    missing = set(LANES) - set(found)
    if missing:
        raise SystemExit(f"missing lanes {sorted(missing)} in:\n{text}")
    return found


def measure(impl: str, rows: int, repeats: int) -> dict[str, tuple[int, float, float]]:
    argv = [DOCKER, "run", "--rm", IMAGES[impl], *PREFIX[impl],
            "--rows", str(rows), "--repeats", str(repeats)]
    return parse(run(argv))


def build_cpp_image() -> None:
    print("building arrowlane-bench-cpp:local", flush=True)
    run([DOCKER, "build", "-q", "-f", str(HERE / "Dockerfile.cpp"),
         "-t", IMAGES["cpp"], str(HERE.parent)])


def image_size(impl: str) -> int:
    text = run([DOCKER, "image", "inspect", IMAGES[impl], "--format", "{{.Size}}"])
    return int(text.strip())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rows", type=int, default=200_000)
    parser.add_argument("--repeats", type=int, default=7)
    parser.add_argument("--check", action="store_true",
                        help="exit 1 when a win condition is missed")
    parser.add_argument("--build", action="store_true",
                        help="rebuild the C++ image first")
    args = parser.parse_args(argv)
    if args.rows < 1 or args.repeats < 1:
        parser.error("rows and repeats must be positive")
    if DOCKER is None:
        raise SystemExit("error: docker is not on PATH")

    if args.build:
        build_cpp_image()

    results: dict[str, dict[str, tuple[int, float, float]]] = {}
    for impl in IMPLS:
        print(f"measuring {impl}", flush=True)
        measure(impl, args.rows, 1)  # warmup, discarded
        results[impl] = measure(impl, args.rows, args.repeats)

    lines: list[str] = []
    losses: list[str] = []
    for condition in CONDITIONS:
        name = condition["name"]
        reference = condition["reference"]
        lane = condition["lane"]
        field = condition["field"]
        value = results["cpp"][lane][field]
        base = results[reference][lane][field]
        limit = base * condition["ratio"]
        verdict = "WIN " if value <= limit else "LOSS"
        if verdict == "LOSS":
            losses.append(name)

        def show(number) -> str:
            return f"{number:.0f}" if field == WIRE else f"{number:.3f}"

        others = " ".join(f"{impl}={show(results[impl][lane][field])}"
                          for impl in IMPLS if impl not in ("cpp", reference))
        line = (f"{verdict} {name:<16} cpp={show(value):<10} "
                f"{reference}={show(base):<10} limit={show(limit):<10} others: {others}")
        lines.append(line)
        print(line, flush=True)

    stamp = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
    report = [
        "# Four way comparison",
        "",
        f"Run {stamp} on {platform.platform()} with "
        f"{platform.processor() or 'unknown cpu'}.",
        f"Rows {args.rows}, repeats {args.repeats}, one lane at a time, "
        "each implementation in its own container.",
        "",
        "## Method",
        "",
        "| Implementation | Method |",
        "|---|---|",
    ]
    for impl in IMPLS:
        report.append(f"| {impl} | {METHOD[impl]} |")
    report += [
        "",
        "All four read the same fixture, defined by the same rules and proven "
        "equal by the canonical digest check.",
        "",
        "## Lane metrics",
        "",
        "| Lane | Impl | Wire bytes | Encode ms | Decode ms |",
        "|---|---|---|---|---|",
    ]
    for lane in LANES:
        for impl in IMPLS:
            wire, enc, dec = results[impl][lane]
            report.append(f"| {lane} | {impl} | {wire} | {enc:.3f} | {dec:.3f} |")
    report += ["", "## Win conditions", "", "```"]
    report += lines
    report += ["```", ""]
    if losses:
        report += [f"Missed: {', '.join(losses)}.", ""]
    else:
        report += ["ALL WIN CONDITIONS MET", ""]
    report += [
        "## Not measured by this harness",
        "",
        "Startup time and peak RSS need a shell or a time utility inside the "
        "image. The Rust and Zig images are scratch images with neither, so "
        "those rows are unproven rather than won. The C++ numbers are in "
        "`docs/performance.md`.",
        "",
        "## Image sizes",
        "",
        "| Impl | Image bytes |",
        "|---|---|",
    ]
    for impl in IMPLS:
        report.append(f"| {impl} | {image_size(impl)} |")
    report.append("")

    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / "latest.md").write_text("\n".join(report), encoding="utf-8")
    (RESULTS / "latest.json").write_text(
        json.dumps({impl: {lane: list(values) for lane, values in lanes.items()}
                    for impl, lanes in results.items()}, indent=2),
        encoding="utf-8",
    )
    print(f"wrote {RESULTS / 'latest.md'}")

    if args.check and losses:
        print(f"MISSED: {', '.join(losses)}")
        return 1
    if args.check:
        print("ALL WIN CONDITIONS MET")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
