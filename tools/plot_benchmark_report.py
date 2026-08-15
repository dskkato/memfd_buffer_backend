#!/usr/bin/env python3
"""Generate figures for BENCHMARK_REPORT.md from the tracked CSV results."""

import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path
from statistics import median

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D


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
PATH_COLORS = {
    "Inter CPU": "#5E81AC",
    "Inter SHM": "#D08770",
    "Intra CPU": "#A3BE8C",
    "Intra SHM": "#B48EAD",
}
SIZES = (64, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304, 16777216)
ONE_MIB = 1048576
METRICS = ("p50_us", "p95_us", "p99_us")
METRIC_LABELS = {
    "p50_us": "p50",
    "p95_us": "p95",
    "p99_us": "p99",
}
METRIC_MARKERS = {
    "p50_us": "o",
    "p95_us": "s",
    "p99_us": "^",
}


def size_label(size):
    for divisor, suffix in ((1024**2, "MiB"), (1024, "KiB")):
        if size >= divisor and size % divisor == 0:
            return f"{size // divisor} {suffix}"
    return f"{size} B"


def read_repeat_results(data_dir):
    values = defaultdict(list)
    for variant in VARIANTS:
        path = data_dir / f"{variant}.csv"
        with path.open(newline="", encoding="utf-8") as stream:
            rows = list(csv.DictReader(stream))
        if len(rows) != 180:
            raise RuntimeError(f"expected 180 rows in {path}, found {len(rows)}")
        for row in rows:
            if row["variant"] != variant:
                raise RuntimeError(
                    f"variant mismatch in {path}: {row['variant']} != {variant}"
                )
            key = (
                row["variant"],
                row["communication"],
                row["backend"],
                int(row["size_bytes"]),
            )
            values[key].append({metric: float(row[metric]) for metric in METRICS})

    return dict(values)


def read_results(data_dir):
    return aggregate_results(read_repeat_results(data_dir))


def aggregate_results(repeat_results):
    return {
        key: {
            metric: median(row[metric] for row in rows)
            for metric in METRICS
        }
        for key, rows in repeat_results.items()
    }


def distribution_x_limit(repeat_results):
    values = [
        row[metric]
        for variant in VARIANTS
        for backend in ("cpu", "memfd")
        for row in repeat_results[(variant, "inter_process", backend, ONE_MIB)]
        for metric in METRICS
    ]
    tick_step = 2000
    return max(tick_step, math.ceil(max(values) * 1.05 / tick_step) * tick_step)


def plot_one_mib_distribution(
    repeat_results, output_dir, backend, backend_label, name, x_limit
):
    figure, axis = plt.subplots(figsize=(10.5, 5.8))
    metric_offsets = {"p50_us": -0.2, "p95_us": 0.0, "p99_us": 0.2}
    repeat_jitter = (-0.055, -0.028, 0.0, 0.028, 0.055)

    for variant_index, variant in enumerate(VARIANTS):
        rows = repeat_results[(variant, "inter_process", backend, ONE_MIB)]
        color = VARIANT_COLORS[variant]
        medians = []
        median_positions = []
        for metric in METRICS:
            values = [row[metric] for row in rows]
            y = variant_index + metric_offsets[metric]
            axis.scatter(
                values,
                [y + jitter for jitter in repeat_jitter],
                color=color,
                alpha=0.28,
                s=24,
                linewidths=0,
                zorder=2,
            )
            medians.append(median(values))
            median_positions.append(y)
            axis.scatter(
                [medians[-1]],
                [y],
                marker=METRIC_MARKERS[metric],
                color=color,
                edgecolors="#2E3440",
                linewidths=0.8,
                s=78,
                zorder=4,
            )
        axis.plot(
            medians,
            median_positions,
            color=color,
            alpha=0.45,
            linewidth=1.4,
            zorder=3,
        )

    axis.set_xlim(0, x_limit)
    axis.set_xticks(range(0, x_limit + 1, 2000))
    axis.set_yticks(range(len(VARIANTS)))
    axis.set_yticklabels([VARIANT_LABELS[variant] for variant in VARIANTS])
    axis.invert_yaxis()
    axis.grid(True, axis="x", color="#D8DEE9", linewidth=0.7)
    axis.grid(True, axis="y", color="#E5E9F0", linewidth=0.5)
    axis.set_axisbelow(True)
    axis.set_xlabel("Latency (µs); shared x-axis across CPU and SHM plots")
    axis.set_ylabel("Variant")
    axis.set_title(f"1 MiB inter-process {backend_label} latency distribution")
    metric_handles = [
        Line2D(
            [0],
            [0],
            marker=METRIC_MARKERS[metric],
            color="#4C566A",
            linestyle="None",
            markersize=7,
            label=METRIC_LABELS[metric],
        )
        for metric in METRICS
    ]
    axis.legend(
        handles=metric_handles,
        title="Reported percentile",
        loc="lower right",
        frameon=True,
    )
    figure.text(
        0.01,
        0.01,
        "Faint points: five repeats; bold markers: median across repeats",
        ha="left",
        va="bottom",
        fontsize=8,
        color="#4C566A",
    )
    figure.tight_layout(rect=(0, 0.04, 1, 1))
    save_figure(figure, output_dir, name)


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
    figure.savefig(output_dir / f"{name}.png", dpi=180, bbox_inches="tight")
    plt.close(figure)


def plot_baseline_paths(results, output_dir):
    figure, axis = plt.subplots(figsize=(11, 6.5))
    for communication, backend, title in PATHS:
        p50 = series(results, "baseline", communication, backend, "p50_us")
        axis.plot(
            SIZES, p50, marker="o", linewidth=2.2, color=PATH_COLORS[title],
            label=title
        )
    configure_axis(axis)
    axis.set_yscale("log")
    axis.set_ylim(bottom=10)
    axis.set_xlabel("Payload size")
    axis.set_ylabel("p50 latency (µs)")
    axis.set_title("Baseline p50 latency across communication and buffer paths")
    axis.legend(frameon=True)
    figure.tight_layout()
    save_figure(figure, output_dir, "baseline-path-latency")


def plot_inter_variant_comparison(results, output_dir, backend, backend_label, name):
    figure, axis = plt.subplots(figsize=(11, 6.5))
    for variant in VARIANTS:
        p50 = series(results, variant, "inter_process", backend, "p50_us")
        p95 = series(results, variant, "inter_process", backend, "p95_us")
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
    axis.set_title(f"Inter-process {backend_label} latency: patch comparison")
    axis.legend(title="p50 (solid); p95 (dashed)", ncol=2, frameon=True)
    figure.tight_layout()
    save_figure(figure, output_dir, name)


def main():
    package_dir = Path(__file__).resolve().parents[1]
    workspace_dir = package_dir.parents[1]
    default_data_dir = workspace_dir / "src/memfd_buffer_backend/benchmark-results-16way"
    default_output_dir = workspace_dir / "src/memfd_buffer_backend/figures"
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--data-dir", type=Path, default=default_data_dir,
        help="benchmark CSV directory (default: src/memfd_buffer_backend/benchmark-results-16way)",
    )
    parser.add_argument(
        "--output-dir", type=Path, default=default_output_dir
    )
    args = parser.parse_args()

    args.data_dir = args.data_dir.expanduser().resolve()
    args.output_dir = args.output_dir.expanduser().resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    repeat_results = read_repeat_results(args.data_dir)
    results = aggregate_results(repeat_results)
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
    plot_inter_variant_comparison(
        results, args.output_dir, "memfd", "SHM", "inter-shm-variant-comparison"
    )
    plot_inter_variant_comparison(
        results, args.output_dir, "cpu", "CPU", "inter-cpu-variant-comparison"
    )
    x_limit = distribution_x_limit(repeat_results)
    plot_one_mib_distribution(
        repeat_results,
        args.output_dir,
        "cpu",
        "CPU",
        "1m-inter-cpu-latency-distribution",
        x_limit,
    )
    plot_one_mib_distribution(
        repeat_results,
        args.output_dir,
        "memfd",
        "SHM",
        "1m-inter-shm-latency-distribution",
        x_limit,
    )
    print(f"aggregated {len(results)} points from {args.data_dir}")
    print(f"distribution plots use shared x-limit of {x_limit} µs")
    print(f"wrote figures to {args.output_dir}")


if __name__ == "__main__":
    main()
