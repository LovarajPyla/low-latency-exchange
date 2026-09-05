#!/usr/bin/env python3
"""
Analyze latency_results.csv produced by benchmarks/latency_benchmark.cpp.

Usage:
    python analyze_latency.py latency_results.csv

Computes p50/p90/p95/p99/p99.9/max and writes a latency distribution plot
and a rolling-throughput plot to disk. Run this on your own measured CSV --
do not fabricate or reuse numbers from someone else's machine.
"""
import sys
import csv
import statistics
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def load_latencies(path: str) -> list[float]:
    latencies = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            latencies.append(float(row["latency_us"]))
    if not latencies:
        raise ValueError(f"No data found in {path}")
    return latencies


def percentile(sorted_data: list[float], p: float) -> float:
    idx = int(p / 100.0 * (len(sorted_data) - 1))
    return sorted_data[idx]


def main():
    if len(sys.argv) < 2:
        print("Usage: python analyze_latency.py <latency_results.csv>")
        sys.exit(1)

    path = sys.argv[1]
    latencies = load_latencies(path)
    sorted_lat = sorted(latencies)

    stats = {
        "count": len(latencies),
        "mean_us": statistics.mean(latencies),
        "p50_us": percentile(sorted_lat, 50),
        "p90_us": percentile(sorted_lat, 90),
        "p95_us": percentile(sorted_lat, 95),
        "p99_us": percentile(sorted_lat, 99),
        "p99.9_us": percentile(sorted_lat, 99.9),
        "max_us": sorted_lat[-1],
    }

    print("========== LATENCY STATS ==========")
    for k, v in stats.items():
        print(f"{k:>10}: {v:.4f}" if isinstance(v, float) else f"{k:>10}: {v}")
    print("====================================")

    out_dir = Path(path).parent

    # Histogram of latency distribution.
    plt.figure(figsize=(8, 5))
    plt.hist(latencies, bins=100, color="#3b6fa0")
    plt.xlabel("Latency (microseconds)")
    plt.ylabel("Count")
    plt.title("Order processing latency distribution")
    plt.tight_layout()
    hist_path = out_dir / "latency_histogram.png"
    plt.savefig(hist_path, dpi=150)
    print(f"Wrote {hist_path}")

    # Latency over the run, to spot warmup effects / drift / GC-like pauses.
    plt.figure(figsize=(8, 5))
    plt.plot(range(len(latencies)), latencies, linewidth=0.3, color="#a03b3b")
    plt.xlabel("Order index")
    plt.ylabel("Latency (microseconds)")
    plt.title("Latency over the course of the run")
    plt.tight_layout()
    series_path = out_dir / "latency_over_time.png"
    plt.savefig(series_path, dpi=150)
    print(f"Wrote {series_path}")


if __name__ == "__main__":
    main()
