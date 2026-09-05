#!/usr/bin/env python3
"""
Compare multiple benchmark runs (e.g. before/after an optimization) side by
side. Point this at a small JSON file you maintain by hand (or generate)
recording each run's measured throughput and percentiles, so every
optimization has a "baseline -> change -> measured result" record, as
recommended in docs/architecture.md.

Expected input JSON shape (benchmark_runs.json):
[
  {"label": "baseline (std::map + vector)", "throughput": 850000, "p99_us": 4.2},
  {"label": "deque instead of vector",      "throughput": 1120000, "p99_us": 2.9}
]

Usage:
    python plot_benchmarks.py benchmark_runs.json
"""
import sys
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def main():
    if len(sys.argv) < 2:
        print("Usage: python plot_benchmarks.py <benchmark_runs.json>")
        sys.exit(1)

    path = Path(sys.argv[1])
    runs = json.loads(path.read_text())
    if not runs:
        print("No runs found in JSON file.")
        sys.exit(1)

    labels = [r["label"] for r in runs]
    throughput = [r["throughput"] for r in runs]
    p99 = [r["p99_us"] for r in runs]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

    ax1.bar(labels, throughput, color="#3b6fa0")
    ax1.set_ylabel("Orders / second")
    ax1.set_title("Throughput by optimization step")
    ax1.tick_params(axis="x", rotation=30)

    ax2.bar(labels, p99, color="#a03b3b")
    ax2.set_ylabel("p99 latency (microseconds)")
    ax2.set_title("p99 latency by optimization step")
    ax2.tick_params(axis="x", rotation=30)

    plt.tight_layout()
    out_path = path.parent / "benchmark_comparison.png"
    plt.savefig(out_path, dpi=150)
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    main()
