"""Rebuild the README figures from saved measurements; does not run benchmarks.

Install matplotlib, then run: python scripts/plot_findings.py
"""

from collections import defaultdict
import json
from pathlib import Path
import re
import statistics

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "docs/bench"
SIDE = DATA / "investigations"
GRAPHS = DATA / "graphs"
BLUE, GREEN, ORANGE = "#2764a5", "#168574", "#c66b24"
plt.rcParams.update({"font.size": 10, "axes.spines.top": False,
                     "axes.spines.right": False, "axes.titleweight": "bold"})


def samples(path):
    groups = defaultdict(list)
    units = {"ns": 1, "us": 1000, "ms": 1_000_000, "s": 1_000_000_000}
    for row in json.loads(path.read_text(encoding="utf-8-sig"))["benchmarks"]:
        if row.get("error_occurred"):
            raise ValueError(row)
        if row.get("run_type") == "aggregate" or "aggregate_name" in row:
            continue
        groups[row.get("run_name", row["name"])].append(row["real_time"] * units[row["time_unit"]])
    return groups


def medians(path):
    return {name: statistics.median(values) for name, values in samples(path).items()}


def save(fig, name, title, note):
    fig.suptitle(title, fontsize=15, fontweight="bold", x=0.04, ha="left")
    fig.text(0.04, 0.02, note, fontsize=9, color="#4b5563", va="bottom")
    fig.tight_layout(rect=(0.015, 0.13, 0.99, 0.92))
    for ax in fig.axes:
        ax.grid(axis="y", alpha=0.15)
        ax.set_axisbelow(True)
    GRAPHS.mkdir(exist_ok=True)
    fig.savefig(GRAPHS / f"{name}.png", dpi=170, facecolor="white")
    plt.close(fig)
    print(f"Saved {name}.png")


def execution():
    values = []
    for backend in ["bytecode", "chunked", "native"]:
        text = (DATA / f"chunk-comparison-{backend}-stat.txt").read_text()
        def number(pattern):
            match = re.search(pattern, text)
            if not match:
                raise ValueError(f"Missing perf field: {pattern}")
            return float(match[1].replace(",", ""))
        values.append([number(r"([\d.]+)\s+\+\-.*seconds time elapsed"),
                       number(r"([\d,]+)\s+instructions:u") / 1e9,
                       number(r"([\d,]+)\s+cycles:u") / 1e9])
    fig, axes = plt.subplots(1, 3, figsize=(11, 4.2))
    for index, (ax, label) in enumerate(zip(axes, ["Process elapsed / s", "Instructions / billions", "Cycles / billions"])):
        bars = ax.bar(["Scalar", "Chunked", "Native"], [v[index] for v in values], color=[ORANGE, GREEN, BLUE])
        ax.bar_label(bars, fmt="%.2f", padding=3)
        ax.set_ylabel(label)
        ax.margins(y=0.2)
    save(fig, "findings_execution", "Less interpreter work, faster execution",
         "Linux i7-8700T | Spread, 4,096 records, 100,000 fixed iterations, five perf runs.\nCounters and elapsed times cover the process. Source: chunk-comparison-*-stat.txt; lower is better.")


def optimisations():
    data = medians(DATA / "program-optimisation-20260928.json")
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.8))
    modes = ["None", "DeadNodes", "ConstantFolding", "CSE", "All"]
    x = np.arange(len(modes))
    for index, count in enumerate([4096, 65536]):
        baseline = data[f"BM_ProgramOptimisationExecute/None/{count}/real_time"]
        times = [baseline / data[f"BM_ProgramOptimisationExecute/{mode}/{count}/real_time"] for mode in modes]
        bars = axes[0].bar(x + (index - 0.5) * 0.37, times, 0.37, label=f"{count:,} records", color=[BLUE, GREEN][index])
        axes[0].bar_label(bars, fmt="%.2f", fontsize=8, padding=2)
    axes[0].set_xticks(x, ["None", "Dead\nnodes", "Constant\nfolding", "CSE", "All"])
    axes[0].set_ylabel("Speedup vs unoptimised / ×")
    axes[0].set_title("Graph passes: deliberately optimisable expression")
    axes[0].set_ylim(0, 1.9)
    axes[0].legend(fontsize=8)
    text = (DATA / "bench-fusion-260930.txt").read_text()
    off, on = [], []
    for count in ["4,096", "65,536"]:
        match = re.search(r"Long execution, " + count + r" records\s+([\d,]+)\s+([\d,]+)", text)
        a, b = [float(v.replace(",", "")) for v in match.groups()]
        off.append(100)
        on.append(b / a * 100)
    x = np.arange(2)
    for index, (name, vals) in enumerate([("Fusion off", off), ("Fusion on", on)]):
        bars = axes[1].bar(x + (index - 0.5) * 0.34, vals, 0.34, label=name, color=[BLUE, GREEN][index])
        axes[1].bar_label(bars, fmt="%.1f%%", padding=2)
    axes[1].set_xticks(x, ["4,096 records", "65,536 records"])
    axes[1].set_ylabel("Execution time / % of fusion off")
    axes[1].set_title("Opcode fusion: compound expression")
    axes[1].set_ylim(0, 120)
    axes[1].legend(fontsize=8)
    save(fig, "findings_optimisation", "Optimise the graph, then fuse bytecode patterns",
         "Windows i7-13700H | Capacity 256. Separate workloads and baselines; gains are not cumulative.\nSources: program-optimisation-20260928.json (10 reps); bench-fusion-260930.txt (20 reps).")


def cache():
    runs = [medians(SIDE / f"ten-equations-run{i}.json") for i in [1, 2]]
    names = ["Midpoint", "Spread", "Product", "SquaredSpread", "WeightedPrice", "PriceRatio", "NormalisedSpread", "QuadraticRatioMix", "ParallelRatios", "HornerPolynomial"]
    fig, axes = plt.subplots(1, 2, figsize=(11, 6.5), gridspec_kw={"width_ratios": [1.35, 1]})
    y = np.arange(len(names))
    for i, data in enumerate(runs):
        ratios = [data[f"{name}/256/real_time"] / data[f"{name}/64/real_time"] for name in names]
        axes[0].scatter(ratios, y + (i - 0.5) * 0.20, color=[BLUE, GREEN][i], label=f"Run {i + 1}", s=35)
    axes[0].set_yticks(y, names)
    axes[0].invert_yaxis()
    axes[0].axvline(1, color="#555", linestyle="--", linewidth=1)
    axes[0].set_xlabel("Time at 256 / time at 64\nAbove 1 favours capacity 64")
    axes[0].set_title("Capacity depends on the expression")
    axes[0].legend()
    x = np.arange(2)
    for index, (filename, label) in enumerate([("scratch-layout-vectorised.json", "Vectorisation enabled"), ("scratch-layout-scalar.json", "Vectorisation disabled")]):
        data = medians(SIDE / filename)
        times = [data[f"HornerPolynomial/{layout}/256/real_time"] / 1000 for layout in ["RegisterMajor", "LaneMajor"]]
        bars = axes[1].bar(x + (index - 0.5) * 0.36, times, 0.36, label=label, color=[GREEN, BLUE][index])
        axes[1].bar_label(bars, fmt="%.0f", padding=3)
    axes[1].set_xticks(x, ["Register-major", "Lane-major"])
    axes[1].set_ylabel("Median batch time / µs")
    axes[1].set_title("Polynomial layout\nCapacity 256")
    axes[1].legend(fontsize=8, loc="upper left")
    axes[1].set_ylim(0, 1040)
    save(fig, "findings_cache", "Scratch layout and chunk capacity both matter",
         "Windows i7-13700H, 65,536 records | Left: two runs, 20 reps each. Right: 15 reps per case.\nSources: investigations/ten-equations-run*.json and scratch-layout-*.json. These are separate experiments.")


def latency():
    lines = (DATA / "latency-harness-260930.txt").read_text().splitlines()
    stats, changes = defaultdict(list), defaultdict(list)
    backend = mode = None
    for line in lines:
        if line in ["Chunked", "Native"]:
            backend = line
        elif backend and " | samples " in line:
            mode = line.split(" | ")[0]
            changes[(backend, mode)].append(float(line.split("change / % ")[1]))
        elif backend and line.startswith("p50 / ns"):
            stats[(backend, mode)].append([float(v) for v in re.findall(r"(?:p50 / ns |p99 / ns |p99.9 / ns )([\d.]+)", line)])
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.8))
    x = np.arange(3)
    for index, backend in enumerate(["Chunked", "Native"]):
        rows = stats[(backend, "TSC Sparse")]
        values = [statistics.median(row[i] for row in rows) for i in range(3)]
        bars = axes[0].bar(x + (index - 0.5) * 0.36, values, 0.36, label=backend, color=[GREEN, BLUE][index])
        axes[0].bar_label(bars, fmt="%.0f", padding=3)
    axes[0].set_xticks(x, ["p50", "p99", "p99.9"])
    axes[0].set_ylabel("256-record batch latency / ns")
    axes[0].set_title("1% TSC sampling: median of five round percentiles")
    axes[0].legend()
    axes[0].set_ylim(0, 350)
    modes = ["Chrono Sparse", "Chrono Full", "TSC Sparse", "TSC Full"]
    x = np.arange(4)
    for index, backend in enumerate(["Chunked", "Native"]):
        values = [statistics.median(changes[(backend, mode)]) for mode in modes]
        low = [v - min(changes[(backend, mode)]) for v, mode in zip(values, modes)]
        high = [max(changes[(backend, mode)]) - v for v, mode in zip(values, modes)]
        axes[1].bar(x + (index - 0.5) * 0.36, values, 0.36, yerr=[low, high], capsize=3, label=backend, color=[GREEN, BLUE][index])
    axes[1].set_xticks(x, ["Chrono\n1%", "Chrono\n100%", "TSC\n1%", "TSC\n100%"])
    axes[1].set_ylabel("Whole-loop time change vs untimed / %")
    axes[1].axhline(0, color="#555", linewidth=0.8)
    axes[1].set_title("Instrumentation changes the workload")
    save(fig, "findings_latency", "Measure the timer as well as the workload",
         "Windows i7-13700H | Midpoint, 256 records. Percentiles include timestamp overhead; no correction.\nSource: latency-harness-260930.txt. Error bars: min–max across rounds; negative changes reflect variability.")


def multicore():
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.8), sharey=True)
    variants = ["Direct", "L0/W4", "L2/W6", "L1/W10"]
    for index, ax in enumerate(axes):
        data = samples(SIDE / f"multicore-confirmation{index + 1}.json")
        for j, name in enumerate(variants):
            times = data[f"Long/{name}/1048576/real_time"]
            throughput = [1048576 / ns for ns in times]  # records/ns = billions/s
            ax.scatter(j + np.linspace(-0.12, 0.12, len(times)), throughput, color=BLUE, alpha=0.5, s=18)
            median = statistics.median(throughput)
            ax.plot([j - 0.22, j + 0.22], [median, median], color=GREEN, linewidth=3)
            ax.text(j, max(throughput) + 0.10, f"{median:.2f}", ha="center", color=GREEN)
        ax.set_xticks(range(4), ["Direct", "4 P-core\nworkers", "6 P-core\nworkers", "10 SMT\nworkers"])
        ax.set_title(f"Confirmation {index + 1}: 20 repetitions")
        ax.set_ylim(0, 2.15)
    axes[0].set_ylabel("Throughput / billion records per second")
    save(fig, "findings_multicore", "IN PROGRESS · Persistent worker prototype",
         "Windows i7-13700H | Compound expression, 1,048,576 records; dots = repetition averages, green = median.\nSix workers use six P cores + an E-core coordinator. Ten SMT workers share five P cores + a P-core coordinator.")


def allocation():
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.8))
    variants = ["CopiedStd", "PmrMonotonicReused", "CustomBumpReused"]
    x = np.arange(3)
    for ax, workload in zip(axes, ["Midpoint", "LongFusion"]):
        for index in range(2):
            data = medians(SIDE / f"allocator-run{index + 1}.json")
            values = [data[f"{workload}/{variant}/real_time"] for variant in variants]
            bars = ax.bar(x + (index - 0.5) * 0.36, values, 0.36, label=f"Run {index + 1}", color=[BLUE, GREEN][index])
            ax.bar_label(bars, fmt="%.0f", padding=3, fontsize=9)
        ax.set_xticks(x, ["Standard", "PMR\narena", "Custom\nbump arena"])
        ax.set_title(workload)
        ax.set_ylabel("Median construction + destruction / ns")
        ax.margins(y=0.2)
        ax.legend()
    save(fig, "findings_allocation", "IN PROGRESS · Reusing preparation storage",
         "Windows i7-13700H | 20 repetitions per run. Initial arena buffer allocation excluded; reset included.\nSources: investigations/allocator-run*.json. Preparation cost, not execution time; long-program baseline varied.")


if __name__ == "__main__":
    execution()
    optimisations()
    cache()
    latency()
    multicore()
    allocation()
