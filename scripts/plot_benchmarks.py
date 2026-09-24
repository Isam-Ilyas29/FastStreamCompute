"""Plot FastStreamCompute Google Benchmark JSON results.

Install once: python -m pip install matplotlib
Run from the project root: python scripts/plot_benchmarks.py
Choose another run: python scripts/plot_benchmarks.py path/to/results.json

Defaults are relative to this script's project, not your terminal's directory.
Images are named after the input file; rerunning replaces that run's images.
"""

import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import re
import statistics
import sys

try:
    import matplotlib

    matplotlib.use("Agg")  # Save images without needing a desktop plot window.
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter
except ImportError:
    raise SystemExit("Install the plotting dependency: python -m pip install matplotlib")


ROOT = Path(__file__).resolve().parents[1]
VARIANTS = ("Native", "BytecodeExecute", "BytecodeCreateExecuteDestroy")
COLORS = ("#147D92", "#D06B23", "#7454A3")
TIME_TO_NS = {"ns": 1, "us": 1_000, "ms": 1_000_000, "s": 1_000_000_000}
NAME_PATTERN = re.compile(
    r"^BM_(.+?)(BytecodeCreateExecuteDestroy|BytecodeCreateDestroy|BytecodeExecute|Native)$"
)


def load_samples(path):
    """Group elapsed times by (formula, function variant, batch size).

    real_time already measures ONE iteration. Never divide it by iterations again.
    Aggregate rows summarize the samples, so including them would double-count.
    """
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    groups = defaultdict(list)
    skipped = set()
    for row in data["benchmarks"]:
        if row.get("run_type") == "aggregate" or "aggregate_name" in row:
            continue
        name = row.get("run_name", row.get("name", ""))
        if row.get("error_occurred"):
            raise ValueError(f"Benchmark failed: {name}: {row.get('error_message', '')}")
        parts = name.split("/")
        match = NAME_PATTERN.fullmatch(parts[0])
        if match is None:
            skipped.add(name)
            continue
        if row.get("threads", 1) != 1:
            raise ValueError(f"This script expects single-thread results: {name}")
        formula, variant = match.groups()
        # Refuse unfamiliar parameters rather than silently mix different cases.
        args = [part for part in parts[1:] if part != "real_time"]
        if variant == "BytecodeCreateDestroy":
            if args:
                raise ValueError(f"Unexpected arguments on creation benchmark: {name}")
            size = None
        else:
            if len(args) != 1 or not args[0].isdigit() or int(args[0]) <= 0:
                raise ValueError(f"Expected one positive batch size: {name}")
            size = int(args[0])
        ns = float(row["real_time"]) * TIME_TO_NS[row["time_unit"]]
        if not math.isfinite(ns) or ns <= 0:
            raise ValueError(f"Expected a finite positive elapsed time: {name}")
        groups[(formula, variant, size)].append(ns)
    if not groups:
        raise ValueError("No supported raw repetition results found; use JSON with raw samples.")
    if skipped:
        print("Warning: skipped unsupported benchmark names:", file=sys.stderr)
        print("\n".join(sorted(skipped)), file=sys.stderr)
    return data.get("context", {}), groups


def decorate(ax):
    ax.grid(axis="y", alpha=0.2)
    ax.set_axisbelow(True)
    ax.spines[["top", "right"]].set_visible(False)


def save(fig, path, source, date, explanation):
    fig.text(
        0.02, 0.015,
        f"{explanation}\nSource: {source.name} | Recorded: {date} | Lower time is better",
        fontsize=9, color="#495563",
    )
    fig.savefig(path, dpi=180, facecolor="white")
    plt.close(fig)
    print(f"Saved {path}")


def draw_performance(formula, groups, output, source, date):
    fig, ax = plt.subplots(figsize=(12, 6))
    fig.subplots_adjust(left=0.09, right=0.97, bottom=0.2, top=0.83)
    for variant, color in zip(VARIANTS, COLORS):
        sizes = sorted(n for f, v, n in groups if f == formula and v == variant)
        if not sizes:
            continue
        # Convert each median batch time into amortized time per record.
        values = [statistics.median(groups[(formula, variant, n)]) / n for n in sizes]
        ax.plot(sizes, values, marker="o", linewidth=2, color=color,
                label=f"BM_{formula}{variant}")
    sizes = sorted({n for f, v, n in groups if f == formula and n is not None})
    ax.set_xscale("log", base=2)
    ax.set_xticks(sizes, [f"{n:,}" for n in sizes])
    ax.set_ylim(bottom=0)
    ax.set_xlabel("Batch size / records (log scale)")
    ax.set_ylabel("Median elapsed time / ns per record")
    ax.set_title(f"{formula}: performance across batch sizes", loc="left", pad=15)
    ax.legend(fontsize=9)
    decorate(ax)
    save(fig, output, source, date,
         "Median of repetition results; time per record = batch time / records. This is not isolated-record latency.")


def box_samples(ax, values, positions, colors):
    """One dot per repetition, plus boxes using the same linear quartiles as the report."""
    boxes = []
    for samples in values:
        median = statistics.median(samples)
        if len(samples) >= 2:
            q1, _, q3 = statistics.quantiles(samples, n=4, method="inclusive")
        else:
            q1 = q3 = median
        iqr = q3 - q1
        within = [v for v in samples if q1 - 1.5 * iqr <= v <= q3 + 1.5 * iqr]
        boxes.append(dict(med=median, q1=q1, q3=q3,
                          whislo=min(within), whishi=max(within), fliers=[]))
    artists = ax.bxp(boxes, positions=positions, widths=0.45, patch_artist=True,
                     showfliers=False, manage_ticks=False,
                     medianprops={"color": "#17212B", "linewidth": 2})
    for patch, color in zip(artists["boxes"], colors):
        patch.set_facecolor(color)
        patch.set_alpha(0.25)
    for position, samples, color in zip(positions, values, colors):
        # Deterministic offsets keep overlapping dots visible; x offset has no meaning.
        offsets = [0] if len(samples) == 1 else [
            (i / (len(samples) - 1) - 0.5) * 0.22 for i in range(len(samples))
        ]
        ax.scatter([position + offset for offset in offsets], samples,
                   color=color, s=16, alpha=0.7, zorder=3)
    decorate(ax)


def draw_variability(formula, groups, output, source, date):
    sizes = sorted({n for f, v, n in groups if f == formula and n is not None})
    fig, axes = plt.subplots(1, len(sizes), figsize=(6 * len(sizes), 7),
                             sharey=True, squeeze=False)
    fig.subplots_adjust(left=0.06, right=0.98, bottom=0.32, top=0.85, wspace=0.1)
    fig.suptitle(f"{formula}: variability across repetitions", x=0.06, ha="left", fontsize=16)
    for ax, n in zip(axes[0], sizes):
        present = [(v, c) for v, c in zip(VARIANTS, COLORS) if (formula, v, n) in groups]
        samples = [[ns / n for ns in groups[(formula, v, n)]] for v, _ in present]
        positions = list(range(1, len(present) + 1))
        box_samples(ax, samples, positions, [c for _, c in present])
        ax.set_xticks(positions, [f"BM_{formula}{v}\n(n={len(vals)})"
                                 for (v, _), vals in zip(present, samples)],
                      rotation=25, ha="right", fontsize=8)
        ax.set_title(f"{n:,} records per batch", fontsize=11)
        ax.set_yscale("log")
        ax.yaxis.set_major_formatter(FuncFormatter(lambda value, _: f"{value:g}"))
        ax.yaxis.set_minor_formatter(FuncFormatter(lambda value, _: f"{value:g}"))
        ax.set_xlim(0.5, len(present) + 0.5)
    axes[0][0].set_ylabel("Elapsed time / ns per record (log scale)")
    save(fig, output, source, date,
         "Dots: repetition averages, not individual-record latencies. Box: middle 50%; line: median; whiskers: 1.5 x IQR limits.")


def draw_creation(groups, performance_path, variability_path, source, date):
    keys = sorted(key for key in groups if key[1] == "BytecodeCreateDestroy")
    labels = [f"BM_{f}{v}" for f, v, _ in keys]
    samples = [groups[key] for key in keys]
    colors = [COLORS[i % len(COLORS)] for i in range(len(keys))]
    positions = list(range(1, len(keys) + 1))
    for variability, path in [(False, performance_path), (True, variability_path)]:
        fig, ax = plt.subplots(figsize=(max(12, len(keys) * 4), 6))
        fig.subplots_adjust(left=0.08, right=0.98, bottom=0.25, top=0.85)
        if variability:
            box_samples(ax, samples, positions, colors)
            title = "Creation and destruction: variability across repetitions"
            note = "Dots: repetition averages; box: middle 50%; line: median; whiskers: 1.5 x IQR limits. No records executed."
        else:
            medians = [statistics.median(values) for values in samples]
            bars = ax.bar(positions, medians, color=colors, width=0.55)
            ax.bar_label(bars, labels=[f"{value:,.1f} ns" for value in medians], padding=5)
            ax.set_ylim(0, max(medians) * 1.2)
            title = "Creation and destruction: median lifecycle cost"
            note = "One iteration constructs and destroys the blueprint/executor. No batch size: no records are executed."
        ax.set_xticks(positions, labels, fontsize=9)
        ax.set_ylabel("Elapsed time / ns per creation-destruction lifecycle")
        ax.set_title(title, loc="left", pad=15)
        decorate(ax)
        save(fig, path, source, date, note)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("json_file", nargs="?", type=Path,
                        default=ROOT / "docs/benchmarks/bench_260923.json")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/benchmarks/graphs")
    args = parser.parse_args()
    try:
        context, groups = load_samples(args.json_file)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"Cannot plot benchmark results: {error}\n")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({"font.size": 11, "axes.titleweight": "bold"})
    date = context.get("date", "not recorded")
    prefix = args.json_file.stem
    for formula in sorted({f for f, v, n in groups if n is not None}):
        safe_name = re.sub(r"[^A-Za-z0-9_-]", "_", formula)
        draw_performance(formula, groups, args.output_dir / f"{prefix}_{safe_name}_performance.png", args.json_file, date)
        draw_variability(formula, groups, args.output_dir / f"{prefix}_{safe_name}_variability.png", args.json_file, date)
    if any(v == "BytecodeCreateDestroy" for f, v, n in groups):
        draw_creation(groups, args.output_dir / f"{prefix}_BytecodeCreateDestroy_performance.png",
                      args.output_dir / f"{prefix}_BytecodeCreateDestroy_variability.png", args.json_file, date)
    print(f"Plotted {len(groups)} cases from {sum(map(len, groups.values()))} raw repetition results.")


if __name__ == "__main__":
    main()
