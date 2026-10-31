"""Compare the four implementations on one fixture and one machine.

Every implementation runs in its own container and prints one line per lane:

    <lane> <wire_bytes> <encode_ms> <decode_ms>

The C++ binary does that with `bench --machine`. The Rust and Zig binaries and
the Python driver already print it, because this is the protocol the reference
repository uses.

Usage:
    python bench/compare.py --rows 200000 --repeats 7 --rounds 3
    python bench/compare.py --rows 200000 --repeats 7 --rounds 3 --check

The harness measures in interleaved rounds: every implementation is measured
inside the same round, so a thermal or load change hits all four together. Each
row reports a median and the range across rounds. When the C++ range and the
reference range overlap, the row prints as UNDETERMINED rather than pretending
a single run settled it.
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
    "cpp": "in process, lane encode plus frame, median of the repeats",
    "python": "in process, pack then unpack, median of the repeats",
    "rust": "in process, median of the repeats",
    "zig": "in process, median of the repeats",
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

Samples = dict[str, dict[str, list[tuple[int, float, float]]]]


def summarise(samples: list[float]) -> tuple[float, float, float]:
    """Median, low and high of the per round samples."""
    ordered = sorted(samples)
    return statistics.median(ordered), ordered[0], ordered[-1]


def field_samples(samples: Samples, impl: str, lane: str, field: int) -> list[float]:
    # Each round contributes one (wire, enc, dec) tuple for this lane.
    return [sample[field] for sample in samples[impl][lane]]


def ranges_overlap(left: tuple[float, float, float],
                   right: tuple[float, float, float]) -> bool:
    return not (left[2] < right[1] or right[2] < left[1])


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
    parser.add_argument("--rounds", type=int, default=3,
                        help="interleaved rounds; each round measures every impl")
    parser.add_argument("--check", action="store_true",
                        help="exit 1 when a win condition is missed")
    parser.add_argument("--build", action="store_true",
                        help="rebuild the C++ image first")
    args = parser.parse_args(argv)
    if args.rows < 1 or args.repeats < 1 or args.rounds < 1:
        parser.error("rows, repeats and rounds must be positive")
    if DOCKER is None:
        raise SystemExit("error: docker is not on PATH")

    if args.build:
        build_cpp_image()

    samples: Samples = {impl: {lane: [] for lane in LANES} for impl in IMPLS}
    for round_index in range(args.rounds):
        for impl in IMPLS:
            print(f"round {round_index + 1}/{args.rounds}: {impl}", flush=True)
            measure(impl, args.rows, 1)  # warmup, discarded
            round_result = measure(impl, args.rows, args.repeats)
            for lane in LANES:
                samples[impl][lane].append(round_result[lane])

    lines: list[str] = []
    losses: list[str] = []
    undetermined: list[str] = []
    for condition in CONDITIONS:
        name = condition["name"]
        reference = condition["reference"]
        lane = condition["lane"]
        field = condition["field"]

        cpp_median, cpp_low, cpp_high = summarise(
            field_samples(samples, "cpp", lane, field))
        ref_median, ref_low, ref_high = summarise(
            field_samples(samples, reference, lane, field))
        limit = ref_median * condition["ratio"]
        overlap = ranges_overlap((cpp_median, cpp_low, cpp_high),
                                 (ref_median, ref_low, ref_high))

        verdict = "WIN " if cpp_median <= limit else "LOSS"
        if overlap:
            verdict = "UNDETERMINED"
            undetermined.append(name)
        if verdict == "LOSS":
            losses.append(name)

        def show(number: float) -> str:
            return f"{number:.0f}" if field == WIRE else f"{number:.3f}"

        others = " ".join(
            f"{impl}={show(summarise(field_samples(samples, impl, lane, field))[0])}"
            for impl in IMPLS if impl not in ("cpp", reference))
        line = (
            f"{verdict:<13} {name:<16} "
            f"cpp={show(cpp_median)} [{show(cpp_low)}..{show(cpp_high)}]  "
            f"{reference}={show(ref_median)} [{show(ref_low)}..{show(ref_high)}]  "
            f"limit={show(limit)}  overlap={'yes' if overlap else 'no'}  "
            f"others: {others}"
        )
        lines.append(line)
        print(line, flush=True)

    stamp = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
    report = [
        "# Four way comparison",
        "",
        f"Run {stamp} on {platform.platform()} with "
        f"{platform.processor() or 'unknown cpu'}.",
        f"Rows {args.rows}, repeats {args.repeats}, rounds {args.rounds}, "
        "implementations interleaved inside each round, each in its own "
        "container.",
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
        "Median of the rounds, with the range in brackets.",
        "",
        "| Lane | Impl | Wire bytes | Encode ms | Decode ms |",
        "|---|---|---|---|---|",
    ]
    for lane in LANES:
        for impl in IMPLS:
            wire = samples[impl][lane][0][WIRE]
            enc = summarise(field_samples(samples, impl, lane, ENCODE))
            dec = summarise(field_samples(samples, impl, lane, DECODE))
            report.append(
                f"| {lane} | {impl} | {wire} | "
                f"{enc[0]:.3f} [{enc[1]:.3f}..{enc[2]:.3f}] | "
                f"{dec[0]:.3f} [{dec[1]:.3f}..{dec[2]:.3f}] |")
    report += ["", "## Win conditions", "", "```"]
    report += lines
    report += ["```", ""]
    if undetermined:
        report += [
            f"Undetermined: {', '.join(undetermined)}. The C++ range and the "
            "reference range overlap, so no single run settles these rows.",
            "",
        ]
    if losses:
        report += [f"Missed: {', '.join(losses)}.", ""]
    elif not undetermined:
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
        json.dumps(
            {impl: {lane: [list(sample) for sample in samples[impl][lane]]
                    for lane in LANES}
             for impl in IMPLS},
            indent=2,
        ),
        encoding="utf-8",
    )
    print(f"wrote {RESULTS / 'latest.md'}")

    if args.check and losses:
        print(f"MISSED: {', '.join(losses)}")
        return 1
    if args.check and undetermined:
        print(f"UNDETERMINED: {', '.join(undetermined)}")
        return 1
    if args.check:
        print("ALL WIN CONDITIONS MET")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
