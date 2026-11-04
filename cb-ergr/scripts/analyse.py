#!/usr/bin/env python3
"""Aggregate sweep results: mean +- 95% CI over seeds, plots against each sweep factor, ablation table, recovery time.

Usage: analyse.py OUT_ROOT [--metrics pdr,energy_per_delivered_j,delay_mean_s] [--kill-time 8000]

Reads OUT_ROOT/<run>/<factor>=<value>/seed<seed>/summary.csv (the layout run_sweep.py writes) and writes into OUT_ROOT/analysis/:
  aggregate.csv           one row per (run, sweep point): mean, std, n and 95% CI half-width of every numeric metric
  <factor>_<metric>.png   metric against the factor, one line per run, error bars = 95% CI
  ablation.md             table of the base point: rows = runs, columns = key metrics (mean +- CI)
  recovery.csv            (with --kill-time and decisions.csv present) recovery time after the failure injection
The 95% CI uses Student's t distribution (critical values from a built-in table, no scipy needed).
"""
import argparse
import glob
import os
import re

import numpy as np
import pandas as pd

T975 = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228,
        11: 2.201, 12: 2.179, 13: 2.160, 14: 2.145, 15: 2.131, 16: 2.120, 17: 2.110, 18: 2.101, 19: 2.093,
        20: 2.086, 25: 2.060, 30: 2.042, 40: 2.021, 60: 2.000, 120: 1.980}
KEY_METRICS = ["pdr", "delay_mean_s", "delay_p95_s", "energy_per_delivered_j", "ack_frames_per_delivered",
               "ctrl_bytes_total_per_delivered", "regret_mean", "fnd_s", "hnd_s", "jain_residual_energy"]


def t_critical(df_degrees):
    """Two-sided 95% critical value of Student's t for the given degrees of freedom."""
    if df_degrees < 1:
        return float("nan")
    keys = sorted(T975)
    for k in keys:
        if df_degrees <= k:
            return T975[k]
    return 1.96


def ci_halfwidth(values):
    n = len(values)
    if n < 2:
        return float("nan")
    return t_critical(n - 1) * np.std(values, ddof=1) / np.sqrt(n)


def load(out_root):
    rows = []
    for path in glob.glob(os.path.join(out_root, "*", "*", "seed*", "summary.csv")):
        parts = path.split(os.sep)
        run, point, seed = parts[-4], parts[-3], parts[-2]
        row = pd.read_csv(path).iloc[0].to_dict()
        row.update(run=run, point=point, seed=int(seed.replace("seed", "")), dir=os.path.dirname(path))
        if "=" in point:
            factor, value = point.split("=", 1)
            row["factor"], row["value"] = factor, float(value)
        else:
            row["factor"], row["value"] = None, np.nan
        rows.append(row)
    if not rows:
        raise SystemExit(f"no summary.csv files found under {out_root}")
    df = pd.DataFrame(rows)
    # fnd_s / hnd_s are -1 when no node (or fewer than half) died during the run: that is "not reached", not a time.
    for column in ("fnd_s", "hnd_s"):
        if column in df.columns:
            df.loc[df[column] < 0, column] = np.nan
    return df


def aggregate(df):
    numeric = [c for c in df.columns if c not in ("run", "point", "factor", "value", "seed", "dir", "selector", "flags", "params")
               and pd.api.types.is_numeric_dtype(df[c])]
    out = []
    for (run, point), g in df.groupby(["run", "point"], sort=False):
        row = {"run": run, "point": point, "factor": g["factor"].iloc[0], "value": g["value"].iloc[0], "n_seeds": len(g)}
        for c in numeric:
            row[c + "_mean"] = g[c].mean()
            row[c + "_ci95"] = ci_halfwidth(g[c].to_numpy())
        out.append(row)
    return pd.DataFrame(out)


def plot_factor(agg, factor, metric, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(5.5, 3.8))
    sub = agg[agg.factor == factor]
    for run, g in sub.groupby("run", sort=False):
        g = g.sort_values("value")
        ax.errorbar(g["value"], g[metric + "_mean"], yerr=g[metric + "_ci95"].fillna(0), marker="o", capsize=3, label=run)
    ax.set_xlabel(factor)
    ax.set_ylabel(metric)
    ax.grid(alpha=0.3)
    ax.legend(fontsize=7)
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def ablation_table(agg, metrics):
    base = agg[agg.factor.isna()]
    if base.empty:  # no base point: use the first sweep value of each run
        base = agg.sort_values("value").groupby("run", sort=False).head(1)
    lines = ["| run | n | " + " | ".join(metrics) + " |", "|---|---|" + "---|" * len(metrics)]
    for _, r in base.iterrows():
        cells = []
        for m in metrics:
            mean, ci = r.get(m + "_mean", np.nan), r.get(m + "_ci95", np.nan)
            cells.append(f"{mean:.3f} +- {ci:.3f}" if np.isfinite(ci) else f"{mean:.3f}")
        lines.append(f"| {r['run']} | {int(r['n_seeds'])} | " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def recovery_time(run_dir, kill_time, window_s=300.0, baseline_s=1000.0, tolerance=0.05):
    """Spec §13.1 recovery time and dip depth, pooled over the affected cells.

    Hop success (ground truth, from decisions.csv) of attempts whose forwarder lies in a cell that lost a head at
    kill_time, in a sliding window of window_s seconds. The baseline is its mean over the baseline_s seconds before
    the kill. The result is (recovery_s, max_drop):
      max_drop   = largest amount the windowed rate falls below the baseline after the kill (>= 0);
      recovery_s = seconds from the kill until the rate is back within `tolerance` (5 points) of the baseline after
                   having dipped more than `tolerance` below it; 0.0 if it never dipped (no measurable effect);
                   NaN if it dipped and had not recovered by the end of the run (right-censored).
    Returns (NaN, NaN) if the needed logs are missing.
    """
    nan = (np.nan, np.nan)
    events_path, decisions_path = os.path.join(run_dir, "events.csv"), os.path.join(run_dir, "decisions.csv")
    if not (os.path.exists(events_path) and os.path.exists(decisions_path)):
        return nan
    events = pd.read_csv(events_path)
    killed = events[events.event == "NODE_KILLED"]
    cells = {int(m.group(1)) for d in killed.details for m in [re.search(r"cell=(\d+)", d)] if m}
    if not cells:
        return nan
    d = pd.read_csv(decisions_path)
    d = d[d.fwd_cell.isin(cells) & (d.learn == 1)].sort_values("time_s")
    if d.empty:
        return nan
    t, ok = d.time_s.to_numpy(), d.truth_received.to_numpy(dtype=float)
    before = (t >= kill_time - baseline_s) & (t < kill_time)
    if before.sum() < 10:
        return nan
    baseline = ok[before].mean()
    dipped, max_drop = False, 0.0
    for tick in np.arange(kill_time, t.max(), 10.0):
        inside = (t > tick - window_s) & (t <= tick)
        if inside.sum() < 5:
            continue
        rate = ok[inside].mean()
        max_drop = max(max_drop, baseline - rate)
        if rate < baseline - tolerance:
            dipped = True
        elif dipped and rate >= baseline - tolerance:
            return tick - kill_time, max_drop
    return (np.nan if dipped else 0.0), max_drop


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out_root")
    ap.add_argument("--metrics", default="pdr,energy_per_delivered_j,delay_mean_s")
    ap.add_argument("--kill-time", type=float, default=None)
    args = ap.parse_args()

    df = load(args.out_root)
    agg = aggregate(df)
    outdir = os.path.join(args.out_root, "analysis")
    os.makedirs(outdir, exist_ok=True)
    agg.to_csv(os.path.join(outdir, "aggregate.csv"), index=False)

    metrics = [m for m in args.metrics.split(",") if m + "_mean" in agg.columns]
    for factor in agg.factor.dropna().unique():
        for m in metrics:
            plot_factor(agg, factor, m, os.path.join(outdir, f"{factor}_{m}.png"))
    table_metrics = [m for m in KEY_METRICS if m + "_mean" in agg.columns]
    with open(os.path.join(outdir, "ablation.md"), "w") as f:
        f.write(ablation_table(agg, table_metrics))

    if args.kill_time is not None:
        results = [recovery_time(d, args.kill_time) for d in df["dir"]]
        df["recovery_s"] = [r[0] for r in results]
        df["max_drop"] = [r[1] for r in results]
        grouped = df.groupby(["run", "point"], sort=False)
        rec = grouped.agg(runs=("seed", "count"), recovery_mean_s=("recovery_s", "mean"), max_drop_mean=("max_drop", "mean")).reset_index()
        rec["censored_runs"] = grouped["recovery_s"].apply(lambda s: s.isna().sum()).values   # dipped, never recovered
        rec.to_csv(os.path.join(outdir, "recovery.csv"), index=False)
        print(rec.to_string(index=False))
    print(f"wrote {outdir}/ (aggregate.csv, ablation.md, plots)")
    print(open(os.path.join(outdir, "ablation.md")).read())


if __name__ == "__main__":
    main()
