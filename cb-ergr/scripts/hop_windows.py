#!/usr/bin/env python3
"""Hop success (ground truth, from decisions.csv) in time windows, per run and sweep point.

Usage: hop_windows.py OUT_ROOT [--windows 300:800,800:1500,...] [--seeds 106,107,108,109,110]
Reads OUT_ROOT/<run>/<point>/seed<S>/decisions.csv (run the sweep with LogDecisions: true) and prints/writes
OUT_ROOT/analysis/hop_windows.csv: mean over seeds of the share of DATA attempts the forwarder really received
(`truth_received`). It shows how fast a method learns (early windows) and how it reacts to a failure injection.
The ground-truth column is used for reporting only.
"""
import argparse, csv, os, sys
import pandas as pd

ap = argparse.ArgumentParser()
ap.add_argument("out_root")
ap.add_argument("--windows", default="300:800,800:1500,1500:2000,2000:2500,2500:3200,3200:4000")
ap.add_argument("--seeds", default="106,107,108,109,110")
args = ap.parse_args()
wins = [tuple(float(x) for x in w.split(":")) for w in args.windows.split(",")]
seeds = [int(s) for s in args.seeds.split(",")]
rows = []
for run in sorted(os.listdir(args.out_root)):
    run_dir = os.path.join(args.out_root, run)
    if run == "analysis" or not os.path.isdir(run_dir):
        continue
    for point in sorted(os.listdir(run_dir)):
        acc = []
        for s in seeds:
            f = os.path.join(run_dir, point, f"seed{s}", "decisions.csv")
            if not os.path.exists(f):
                continue
            d = pd.read_csv(f, usecols=["time_s", "truth_received"])
            acc.append([d[(d.time_s >= a) & (d.time_s < b)].truth_received.mean() for a, b in wins])
        if acc:
            mean = pd.DataFrame(acc).mean().tolist()
            rows.append([run, point, len(acc)] + [round(v, 4) for v in mean])
header = ["run", "point", "seeds"] + [f"{int(a)}-{int(b)}s" for a, b in wins]
os.makedirs(os.path.join(args.out_root, "analysis"), exist_ok=True)
with open(os.path.join(args.out_root, "analysis", "hop_windows.csv"), "w", newline="") as f:
    w = csv.writer(f); w.writerow(header); w.writerows(rows)
print(",".join(header))
for r in rows:
    print(",".join(str(x) for x in r))
