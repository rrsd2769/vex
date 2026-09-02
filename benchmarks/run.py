#!/usr/bin/env python3
"""Times each benchmarks/*.vx program against its benchmarks/*.py twin.

Usage: python3 benchmarks/run.py [--runs N] [--vex PATH]

Requires a Release build of vex (the Debug build has ASan/UBSan on, which
by itself accounts for roughly an order of magnitude of slowdown -- see
ROADMAP.md week 8 and the week-7 handoff):

    cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build-release

Each program is run --runs times (default 7); the first run is discarded
as a warmup and the rest are reported as the median wall-clock time. Every
pair's stdout is also diffed -- a benchmark that "wins" by computing the
wrong answer isn't a benchmark.
"""
import argparse
import statistics
import subprocess
import sys
import time
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
PAIRS = ["fib", "loop_sum", "string_eq"]


def timed_run(argv):
    start = time.perf_counter()
    result = subprocess.run(argv, capture_output=True, text=True, check=True)
    elapsed = time.perf_counter() - start
    return elapsed, result.stdout.strip()


def median_of(argv, runs):
    samples = []
    output = None
    for i in range(runs):
        elapsed, out = timed_run(argv)
        if i == 0:
            continue  # warmup: discard first run (cold cache / page faults)
        samples.append(elapsed)
        output = out
    return statistics.median(samples), output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=int, default=7, help="runs per program, including 1 discarded warmup")
    parser.add_argument("--vex", default=str(BENCH_DIR.parent / "build-release" / "vex"), help="path to the Release vex binary")
    parser.add_argument("--python", default=sys.executable, help="python interpreter to compare against")
    args = parser.parse_args()

    vex_path = Path(args.vex)
    if not vex_path.exists():
        sys.exit(f"error: {vex_path} not found -- build a Release binary first (see this script's docstring)")

    rows = []
    for name in PAIRS:
        vx_file = BENCH_DIR / f"{name}.vx"
        py_file = BENCH_DIR / f"{name}.py"

        vx_time, vx_out = median_of([str(vex_path), str(vx_file)], args.runs)
        py_time, py_out = median_of([args.python, str(py_file)], args.runs)

        if vx_out != py_out:
            sys.exit(f"error: {name} outputs disagree -- vex: {vx_out!r}, python: {py_out!r}")

        rows.append((name, vx_time, py_time, vx_out))

    name_w = max(len(r[0]) for r in rows)
    print(f"{'benchmark':<{name_w}}  {'vex':>10}  {'python':>10}  {'ratio':>8}  output")
    for name, vx_time, py_time, out in rows:
        ratio = py_time / vx_time
        direction = "faster" if ratio >= 1 else "slower"
        print(f"{name:<{name_w}}  {vx_time * 1000:>8.1f}ms  {py_time * 1000:>8.1f}ms  {ratio:>6.2f}x {direction:<6}  {out}")


if __name__ == "__main__":
    main()
