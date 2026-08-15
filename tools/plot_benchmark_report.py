#!/usr/bin/env python3
"""Generate figures for BENCHMARK_REPORT.md from the tracked CSV results."""

import argparse
import csv
from collections import defaultdict
from pathlib import Path
from statistics import median

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


VARIANTS = ("baseline", "unique_ptr", "lazy", "reserve")
VARIANT_LABELS = {
    "baseline": "baseline",
    "unique_ptr": "unique_ptr",
    "lazy": "lazy",
    "reserve": "reserve",
}
VARIANT_COLORS = {
    "baseline": "#4C566A",
    "unique_ptr": "#5E81AC",
    "lazy": "#A3BE8C",
    "reserve": "#D08770",
}
PATHS = (
    ("inter_process", "cpu", "Inter CPU"),
    ("inter_process", "memfd", "Inter SHM"),
    ("intra_process_va", "cpu", "Intra CPU"),
    ("intra_process_va", "memfd", "Intra SHM"),
)
SIZES = (64, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304, 16777216)


def size_label(size):
    for divisor, suffix in ((1024**2, "MiB"), (1024, "KiB")):
        if size >= divisor and size % divisor == 0:
            return f"{size // divisor} {suffix}"
    return f"{size} B"


def read_results(data_dir):
    values = defaultdict(lambda: defaultdict(list))
    for variant in VARIANTS:
        path = data_dir / f"{variant}.csv"
        with path.open(newline="", encoding="utf-8") as stream:
            for row in csv.DictReader(stream):
                key = (
                    row["variant"],
                    row["communication"],
                    row["backend"],
                    int(row["size_bytes"]),
                )
                values[key]["p50_us"].append(float(row["p50_us"]))
                values[key]["p95_us"].append(float(row["p95_us"]))

    return {
        key: {metric: median(samples) for metric, samples in metrics.items()}
        for key, metrics in values.items()
    }


def series(results, variant, communication, backend, metric):
    return [
        results[(variant, communication, backend, size)][metric]
        for size in SIZES
    ]


def configure_axis(axis):
    axis.set_xscale("log", base=2)
    axis.set_xticks(SIZES)
    axis.set_xticklabels([size_label(size) for size in SIZES], rotation=35, ha="right")
    axis.grid(True, which="major", axis="both", color="#D8DEE9", linewidth=0.7)
    axis.set_axisbelow(True)


def save_figure(figure, output_dir, name):
    figure.savefig(output_dir / f"{name}.svg", bbox_inches="tight")
    figure.savefig(output_dir / f"{name}.png", dpi=180, bbox_inches="tight")
    plt.close(figure)


def plot_baseline_paths(results, output_dir):
    figure, axes = plt.subplots(2, 2, figsize=(13, 8), sharex=True)
    figure.suptitle("Baseline latency across communication and buffer paths", fontsize=15)
    for axis, (communication, backend, title) in zip(axes.flat, PATHS):
        p50 = series(results, "baseline", communication, backend, "p50_us")
        p95 = series(results, "baseline", communication, backend, "p95_us")
        axis.plot(SIZES, p50, marker="o", linewidth=2, color="#5E81AC", label="p50")
        axis.plot(
            SIZES, p95, marker="o", linestyle="--", linewidth=1.4,
            color="#D08770", label="p95"
        )
        configure_axis(axis)
        axis.set_title(title)
        axis.set_yscale("log")
        axis.set_ylabel("Latency (µs)")
        axis.set_ylim(bottom=10)
    axes[1, 0].set_xlabel("Payload size")
    axes[1, 1].set_xlabel("Payload size")
    axes[0, 1].legend(loc="upper left", frameon=True)
    figure.tight_layout(rect=(0, 0, 1, 0.95))
    save_figure(figure, output_dir, "baseline-path-latency")


def plot_inter_shm_variants(results, output_dir):
    figure, axis = plt.subplots(figsize=(11, 6.5))
    for variant in VARIANTS:
        p50 = series(results, variant, "inter_process", "memfd", "p50_us")
        p95 = series(results, variant, "inter_process", "memfd", "p95_us")
        color = VARIANT_COLORS[variant]
        axis.plot(
            SIZES, p50, marker="o", linewidth=2.2, color=color,
            label=VARIANT_LABELS[variant]
        )
        axis.plot(SIZES, p95, linestyle="--", linewidth=1.0, alpha=0.65, color=color)
    configure_axis(axis)
    axis.set_yscale("log")
    axis.set_ylim(bottom=500)
    axis.set_xlabel("Payload size")
    axis.set_ylabel("Latency (µs)")
    axis.set_title("Inter-process SHM latency: patch comparison")
    axis.legend(title="p50 (solid); p95 (dashed)", ncol=2, frameon=True)
    figure.tight_layout()
    save_figure(figure, output_dir, "inter-shm-variant-comparison")


def plot_shm_speedup(results, output_dir):
    figure, axis = plt.subplots(figsize=(11, 6.5))
    for variant in VARIANTS:
        cpu = series(results, variant, "inter_process", "cpu", "p50_us")
        shm = series(results, variant, "inter_process", "memfd", "p50_us")
        speedup = [cpu_value / shm_value for cpu_value, shm_value in zip(cpu, shm)]
        axis.plot(
            SIZES, speedup, marker="o", linewidth=2.2,
            color=VARIANT_COLORS[variant], label=VARIANT_LABELS[variant]
        )
    configure_axis(axis)
    axis.set_xlabel("Payload size")
    axis.set_ylabel("CPU p50 / SHM p50 (×)")
    axis.set_title("Inter-process benefit of the memfd-backed buffer")
    axis.axhline(1.0, color="#4C566A", linewidth=1, linestyle=":")
    axis.legend(frameon=True)
    figure.tight_layout()
    save_figure(figure, output_dir, "inter-shm-speedup")


def main():
    package_dir = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--data-dir", type=Path, default=package_dir / "benchmark-results-16way"
    )
    parser.add_argument(
        "--output-dir", type=Path, default=package_dir / "figures"
    )
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    results = read_results(args.data_dir)
    expected = len(VARIANTS) * len(PATHS) * len(SIZES)
    if len(results) != expected:
        raise RuntimeError(f"expected {expected} aggregated points, found {len(results)}")

    plt.rcParams.update({
        "font.family": "DejaVu Sans",
        "font.size": 10,
        "axes.titlesize": 12,
        "axes.labelsize": 10,
        "figure.facecolor": "white",
        "axes.facecolor": "#FFFFFF",
    })
    plot_baseline_paths(results, args.output_dir)
    plot_inter_shm_variants(results, args.output_dir)
    plot_shm_speedup(results, args.output_dir)
    print(f"wrote figures to {args.output_dir}")


if __name__ == "__main__":
    main()
