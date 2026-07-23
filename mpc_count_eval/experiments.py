"""The five experiments comparing the local-enumeration construction against
ground truth and against the standard direct-hashing baseline.

Each exp_* function writes CSV(s) and PNG plot(s) into `outdir` and returns a
dict of whatever summary data is useful for the top-level report.
"""
import math
import os
import random

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from instance_gen import make_instance_pair
from bruteforce import solve
from estimators import local_enumeration_construction, standard_approxmc, find_i1

# Categorical palette (validated CVD-safe ordering), used consistently.
BLUE, ORANGE, AQUA, YELLOW, MAGENTA, GREEN, VIOLET, RED = (
    "#2a78d6", "#eb6834", "#1baf7a", "#eda100",
    "#e87ba4", "#008300", "#4a3aa7", "#e34948",
)

N = 14
THRESH = 100
DELTA = 0.05

plt.rcParams.update({
    "figure.facecolor": "white",
    "axes.facecolor": "white",
    "axes.edgecolor": "#52514e",
    "axes.labelcolor": "#0b0b0b",
    "text.color": "#0b0b0b",
    "xtick.color": "#52514e",
    "ytick.color": "#52514e",
    "axes.grid": True,
    "grid.color": "#e5e4df",
    "grid.linewidth": 0.6,
    "font.size": 10,
})


def build_instance(n, m1, m2, w, f, seed):
    rng_py = random.Random(seed)
    phi1, phi2 = make_instance_pair(n, m1, m2, w, f, rng=rng_py)
    sol1 = solve(phi1, n)
    sol2 = solve(phi2, n)
    sol_and = sol1 & sol2
    S1, S2, k = int(sol1.sum()), int(sol2.sum()), int(sol_and.sum())
    return dict(phi1=phi1, phi2=phi2, sol1=sol1, sol2=sol2, sol_and=sol_and,
                S1=S1, S2=S2, k=k, n=n, m1=m1, m2=m2, w=w, f=f, seed=seed)


def rel_err(est, k):
    return abs(est - k) / max(k, 1)


# ---------------------------------------------------------------------------
# Experiment 1: healthy vs. degenerate regime
# ---------------------------------------------------------------------------

EXP1_CONFIGS = [
    # (label, m1, m2, w, f)
    ("healthy",    30, 20, 3, 0.4),
    ("medium",     30, 40, 3, 0.8),
    ("low",        30, 20, 3, 1.0),
    ("degenerate", 30, 40, 3, 1.0),
]


def exp1_healthy_vs_degenerate(outdir, seed=42, itercount=300):
    rows = []
    hist_data = []
    for label, m1, m2, w, f in EXP1_CONFIGS:
        inst = build_instance(N, m1, m2, w, f, seed)
        rng = np.random.default_rng(seed)
        res = local_enumeration_construction(
            inst["sol1"], inst["sol2"], N, THRESH, itercount, rng, delta=DELTA)
        mu = inst["k"] * (2.0 ** -res["i1"])
        rows.append(dict(
            label=label, f=f, m2=m2, S1=inst["S1"], S2=inst["S2"], k=inst["k"],
            i1=res["i1"], mu=mu, mean_k_hat=res["mean_k_hat"],
            median_k_hat=res["median_k_hat"],
            rel_err_mean=rel_err(res["mean_k_hat"], inst["k"]),
            rel_err_median=rel_err(res["median_k_hat"], inst["k"]),
        ))
        hist_data.append((label, mu, inst["k"], res["k_hats"]))

    df = pd.DataFrame(rows)
    df.to_csv(os.path.join(outdir, "exp1_healthy_vs_degenerate.csv"), index=False)

    fig, axes = plt.subplots(1, len(hist_data), figsize=(4.2 * len(hist_data), 3.6), sharey=False)
    for ax, (label, mu, k, k_hats) in zip(axes, hist_data):
        ax.hist(k_hats, bins=20, color=BLUE, alpha=0.85, edgecolor="white", linewidth=0.5)
        ax.axvline(k, color=RED, linewidth=2, label=f"true k={k}")
        ax.axvline(np.mean(k_hats), color=ORANGE, linewidth=2, linestyle="--", label="mean")
        ax.axvline(np.median(k_hats), color=VIOLET, linewidth=2, linestyle=":", label="median")
        ax.set_title(f"{label}\n(mu={mu:.2f})")
        ax.set_xlabel("k_hat")
        ax.legend(fontsize=8)
    axes[0].set_ylabel("count across repetitions")
    fig.suptitle("Local-enumeration construction: k_hat distribution vs. mu = k * 2^-i1")
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "exp1_histograms.png"), dpi=150)
    plt.close(fig)

    return df


# ---------------------------------------------------------------------------
# Experiment 2: zero-hits bound tightness
# ---------------------------------------------------------------------------

def exp2_zero_hits_bound_tightness(outdir, n_instances=40, itercount=60, seed0=1000):
    rows = []
    for idx in range(n_instances):
        seed = seed0 + idx
        m1, m2, w, f = 30, 40, 3, 1.0
        inst = build_instance(N, m1, m2, w, f, seed)
        rng = np.random.default_rng(seed)
        res = local_enumeration_construction(
            inst["sol1"], inst["sol2"], N, THRESH, itercount, rng, delta=DELTA)
        k = inst["k"]
        if k == 0:
            continue  # ratio bound/true-k undefined when true k is exactly 0
        zero_reps = np.where(res["xs"] == 0)[0]
        for r in zero_reps:
            chernoff_ratio = res["chernoff_ubs"][r] / k
            cheby_ratio = res["cheby_ub"] / k
            rows.append(dict(seed=seed, k=k, i1=res["i1"],
                              chernoff_ratio=chernoff_ratio, cheby_ratio=cheby_ratio,
                              chernoff_violated=chernoff_ratio < 1.0))

    df = pd.DataFrame(rows)
    df.to_csv(os.path.join(outdir, "exp2_zero_hits_bound_tightness.csv"), index=False)

    fig, ax = plt.subplots(figsize=(6.5, 4.2))
    bins = np.logspace(np.log10(max(df[["chernoff_ratio", "cheby_ratio"]].min().min(), 1e-1)),
                        np.log10(df[["chernoff_ratio", "cheby_ratio"]].max().max()), 30)
    ax.hist(df["chernoff_ratio"], bins=bins, alpha=0.75, color=BLUE, label="Chernoff-style (2^i1 * ln(1/delta))")
    ax.hist(df["cheby_ratio"], bins=bins, alpha=0.75, color=ORANGE, label="Chebyshev/Markov (S1/(thresh*delta))")
    ax.set_xscale("log")
    ax.set_xlabel("bound / true k  (ratio, log scale)")
    ax.set_ylabel("count (over x'=0 repetitions, low-overlap instances)")
    ax.set_title("Zero-hits upper-bound tightness (delta=0.05)")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "exp2_bound_ratio_hist.png"), dpi=150)
    plt.close(fig)

    summary = df[["chernoff_ratio", "cheby_ratio"]].agg(["mean", "median", "min", "max"])
    summary.loc["violation_rate", "chernoff_ratio"] = df["chernoff_violated"].mean()
    summary.loc["violation_rate", "cheby_ratio"] = (df["cheby_ratio"] < 1.0).mean()
    summary.to_csv(os.path.join(outdir, "exp2_summary.csv"))
    return df, summary


# ---------------------------------------------------------------------------
# Experiment 3: low-count regime spread
# ---------------------------------------------------------------------------

EXP3_CONFIGS = [
    ("healthy (mu~thresh/4)", 30, 20, 3, 0.4),
    ("medium (mu~3)",         30, 40, 3, 0.8),
    ("low-count (mu~1-3)",    30, 20, 3, 1.0),
]


def exp3_low_count_regime(outdir, seed=42, itercount=400):
    rows = []
    for label, m1, m2, w, f in EXP3_CONFIGS:
        inst = build_instance(N, m1, m2, w, f, seed)
        rng = np.random.default_rng(seed)
        res = local_enumeration_construction(
            inst["sol1"], inst["sol2"], N, THRESH, itercount, rng, delta=DELTA)
        k_hats = res["k_hats"]
        mu = inst["k"] * (2.0 ** -res["i1"])
        mean_, median_, std_ = np.mean(k_hats), np.median(k_hats), np.std(k_hats)
        q75, q25 = np.percentile(k_hats, [75, 25])
        cv = std_ / mean_ if mean_ > 0 else float("inf")
        iqr_over_median = (q75 - q25) / median_ if median_ > 0 else float("inf")
        frac_zero = float(np.mean(res["xs"] == 0))
        rows.append(dict(
            label=label, k=inst["k"], i1=res["i1"], mu=mu,
            mean_k_hat=mean_, median_k_hat=median_, std_k_hat=std_,
            coeff_of_variation=cv, iqr_over_median=iqr_over_median,
            frac_reps_x_prime_zero=frac_zero,
        ))
    df = pd.DataFrame(rows)
    df.to_csv(os.path.join(outdir, "exp3_low_count_regime.csv"), index=False)

    fig, ax = plt.subplots(figsize=(6.5, 4.2))
    ax.bar(df["label"], df["coeff_of_variation"], color=[BLUE, ORANGE, AQUA])
    ax.set_ylabel("coefficient of variation: std(k_hat) / mean(k_hat)")
    ax.set_title("Spread of k_hat across repetitions, by regime")
    fig.autofmt_xdate(rotation=15)
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "exp3_spread_by_regime.png"), dpi=150)
    plt.close(fig)

    return df


# ---------------------------------------------------------------------------
# Experiment 4: comparison to the direct two-sided baseline
# ---------------------------------------------------------------------------

EXP4_CONFIGS = [
    ("sparse, f=0.0",  15, 15, 3, 0.0),
    ("sparse, f=0.5",  15, 15, 3, 0.5),
    ("sparse, f=1.0",  15, 15, 3, 1.0),
    ("dense, f=0.0",   30, 30, 3, 0.0),
    ("dense, f=0.5",   30, 30, 3, 0.5),
    ("dense, f=1.0",   30, 30, 3, 1.0),
]


def exp4_baseline_comparison(outdir, seed=7, local_itercount=100, baseline_itercount=25):
    rows = []
    for label, m1, m2, w, f in EXP4_CONFIGS:
        inst = build_instance(N, m1, m2, w, f, seed)
        k = inst["k"]

        rng_local = np.random.default_rng(seed)
        local_res = local_enumeration_construction(
            inst["sol1"], inst["sol2"], N, THRESH, local_itercount, rng_local, delta=DELTA)

        rng_base = np.random.default_rng(seed + 1)
        base_res = standard_approxmc(inst["sol_and"], N, THRESH, baseline_itercount, rng_base)

        rows.append(dict(
            label=label, k=k,
            local_mean=local_res["mean_k_hat"], local_median=local_res["median_k_hat"],
            baseline_median=base_res["median_k_hat"],
            rel_err_local_mean=rel_err(local_res["mean_k_hat"], k),
            rel_err_local_median=rel_err(local_res["median_k_hat"], k),
            rel_err_baseline_median=rel_err(base_res["median_k_hat"], k),
        ))
    df = pd.DataFrame(rows)
    df.to_csv(os.path.join(outdir, "exp4_baseline_comparison.csv"), index=False)

    x = np.arange(len(df))
    width = 0.25
    fig, ax = plt.subplots(figsize=(9, 4.5))
    ax.bar(x - width, df["rel_err_local_mean"], width, label="local-enum (mean)", color=BLUE)
    ax.bar(x, df["rel_err_local_median"], width, label="local-enum (median)", color=VIOLET)
    ax.bar(x + width, df["rel_err_baseline_median"], width, label="baseline (median)", color=ORANGE)
    ax.set_xticks(x)
    ax.set_xticklabels(df["label"], rotation=20, ha="right")
    ax.set_ylabel("relative error vs. true k")
    ax.set_title("Local-enumeration construction vs. direct two-sided baseline")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "exp4_relative_error_comparison.png"), dpi=150)
    plt.close(fig)

    return df


# ---------------------------------------------------------------------------
# Experiment 5: density x correlation sweep
# ---------------------------------------------------------------------------

F_GRID = [0.0, 0.1, 0.3, 0.5, 0.7, 1.0]
DENSITY_GRID = [("sparse", 15), ("medium", 30), ("dense", 35)]


def exp5_sweep(outdir, n_seeds=3, local_itercount=40, baseline_itercount=15, seed0=5000):
    rows = []
    for dens_label, m in DENSITY_GRID:
        for f in F_GRID:
            rel_err_local_means, rel_err_local_medians, rel_err_baseline = [], [], []
            chernoff_ratios = []
            for s in range(n_seeds):
                seed = seed0 + s
                inst = build_instance(N, m, m, 3, f, seed)
                k = inst["k"]

                rng_local = np.random.default_rng(seed)
                local_res = local_enumeration_construction(
                    inst["sol1"], inst["sol2"], N, THRESH, local_itercount, rng_local, delta=DELTA)
                rel_err_local_means.append(rel_err(local_res["mean_k_hat"], k))
                rel_err_local_medians.append(rel_err(local_res["median_k_hat"], k))

                rng_base = np.random.default_rng(seed + 1)
                base_res = standard_approxmc(inst["sol_and"], N, THRESH, baseline_itercount, rng_base)
                rel_err_baseline.append(rel_err(base_res["median_k_hat"], k))

                if k > 0:
                    zero_reps = np.where(local_res["xs"] == 0)[0]
                    for r in zero_reps:
                        chernoff_ratios.append(local_res["chernoff_ubs"][r] / k)

            chernoff_ratios = np.array(chernoff_ratios, dtype=float)
            rows.append(dict(
                density=dens_label, m=m, f=f,
                rel_err_local_mean=np.mean(rel_err_local_means),
                rel_err_local_median=np.mean(rel_err_local_medians),
                rel_err_baseline_median=np.mean(rel_err_baseline),
                mean_chernoff_bound_ratio=(chernoff_ratios.mean() if len(chernoff_ratios) else np.nan),
                chernoff_violation_rate=((chernoff_ratios < 1.0).mean() if len(chernoff_ratios) else np.nan),
                n_zero_hit_reps=len(chernoff_ratios),
            ))
    df = pd.DataFrame(rows)
    df.to_csv(os.path.join(outdir, "exp5_sweep.csv"), index=False)

    def heatmap(value_col, title, fname, cmap="Blues", log=False):
        pivot = df.pivot(index="density", columns="f", values=value_col)
        pivot = pivot.reindex(["sparse", "medium", "dense"])
        fig, ax = plt.subplots(figsize=(7, 3.8))
        data = np.log10(pivot.values) if log else pivot.values
        im = ax.imshow(data, cmap=cmap, aspect="auto")
        ax.set_xticks(range(len(pivot.columns)))
        ax.set_xticklabels([f"{c:.1f}" for c in pivot.columns])
        ax.set_yticks(range(len(pivot.index)))
        ax.set_yticklabels(pivot.index)
        ax.set_xlabel("correlation knob f")
        ax.set_ylabel("density")
        for i in range(pivot.shape[0]):
            for j in range(pivot.shape[1]):
                v = pivot.values[i, j]
                txt = "n/a" if np.isnan(v) else (f"{v:.2g}")
                ax.text(j, i, txt, ha="center", va="center", fontsize=8,
                        color="white" if not np.isnan(v) and data[i, j] > np.nanmean(data) else "black")
        ax.set_title(title)
        fig.colorbar(im, ax=ax, shrink=0.85)
        fig.tight_layout()
        fig.savefig(os.path.join(outdir, fname), dpi=150)
        plt.close(fig)

    heatmap("rel_err_local_mean", "Local-enum (mean) relative error", "exp5_heatmap_local_mean_relerr.png")
    heatmap("rel_err_local_median", "Local-enum (median) relative error", "exp5_heatmap_local_median_relerr.png")
    heatmap("rel_err_baseline_median", "Baseline (median) relative error", "exp5_heatmap_baseline_relerr.png")
    heatmap("mean_chernoff_bound_ratio", "Mean zero-hits Chernoff bound / true k (x'=0 reps)",
            "exp5_heatmap_bound_ratio.png", cmap="Oranges")
    heatmap("chernoff_violation_rate",
            "Chernoff bound violation rate (fraction of x'=0 reps with bound < true k)",
            "exp5_heatmap_violation_rate.png", cmap="Reds")

    return df


def run_all(outdir):
    os.makedirs(outdir, exist_ok=True)
    results = {}
    print("Running experiment 1: healthy vs degenerate regime...")
    results["exp1"] = exp1_healthy_vs_degenerate(outdir)
    print("Running experiment 2: zero-hits bound tightness...")
    results["exp2"], results["exp2_summary"] = exp2_zero_hits_bound_tightness(outdir)
    print("Running experiment 3: low-count regime spread...")
    results["exp3"] = exp3_low_count_regime(outdir)
    print("Running experiment 4: baseline comparison...")
    results["exp4"] = exp4_baseline_comparison(outdir)
    print("Running experiment 5: density x correlation sweep...")
    results["exp5"] = exp5_sweep(outdir)
    return results
