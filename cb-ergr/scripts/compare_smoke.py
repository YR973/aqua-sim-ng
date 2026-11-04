#!/usr/bin/env python3
"""Run a few selector variants on several seeds in parallel and print a comparison table (kickoff D5).

Usage: compare_smoke.py BINARY OUTROOT [--rate 0.004] [--seeds 101,102,103] [--n 100] [--sim 2500]
Results of each run go to OUTROOT/<variant>_<seed>/; the table is also written to OUTROOT/compare.csv.
Training seeds (101-110) are the default so nothing here touches the test seeds 1-20.
"""
import argparse, subprocess, sys, os
from concurrent.futures import ThreadPoolExecutor
import pandas as pd

VARIANTS = {
    # name: (selector, selector attributes)
    "RR": ("RoundRobin", ""),
    "RR+FB": ("RoundRobin", "WithFeedback=true"),
    "CB": ("CbV5", ""),
    "CB-ackEvery": ("CbV5", "AckMode=Every"),
    "CB-noShare": ("CbV5", "EnableSharing=false"),
    "FW": ("FixedWeight", ""),
    "QL": ("QLearning", ""),
    "CARP": ("CarpEwma", ""),
    "LinQ": ("LfaQ", ""),
}
COLUMNS = ["pdr", "delay_mean_s", "attempts_total", "retx_per_delivered", "hop_success_truth", "ar_rate",
           "ack_frames_per_delivered", "label_ack_frac", "false_negative_rate",
           "ctrl_bytes_total_per_delivered", "energy_per_delivered_j"]


def run_one(binary, outroot, variant, seed, args):
    selector, attrs = VARIANTS[variant]
    out = os.path.join(outroot, f"{variant}_{seed}")
    cmd = [binary, f"--Selector={selector}", f"--Seed={seed}", f"--N={args.n}", f"--SimTimeS={args.sim}",
           f"--WarmupS={args.warmup}", f"--PktRatePerNodeHz={args.rate}", f"--InitialEnergyJ={args.e0}",
           f"--CurrentSpeed={args.speed}", f"--OutDir={out}", "--LogLinks=false", "--LogNodes=false",
           "--LogPackets=false"]
    if attrs:
        cmd.append(f"--SelectorAttributes={attrs}")
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(f"FAILED {variant} {seed}: {r.stderr[-300:]}", file=sys.stderr)
        return None
    row = pd.read_csv(os.path.join(out, "summary.csv")).iloc[0]
    return {"variant": variant, "seed": seed, **{c: float(row[c]) for c in COLUMNS}}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("outroot")
    ap.add_argument("--variants", default="RR,RR+FB,CB")
    ap.add_argument("--seeds", default="101,102,103")
    ap.add_argument("--rate", type=float, default=0.004)
    ap.add_argument("--n", type=int, default=100)
    ap.add_argument("--sim", type=float, default=2500)
    ap.add_argument("--warmup", type=float, default=500)
    ap.add_argument("--e0", type=float, default=10000)
    ap.add_argument("--speed", type=float, default=1.0)
    ap.add_argument("--jobs", type=int, default=3)
    args = ap.parse_args()
    os.makedirs(args.outroot, exist_ok=True)
    jobs = [(v, int(s)) for v in args.variants.split(",") for s in args.seeds.split(",")]
    with ThreadPoolExecutor(args.jobs) as pool:
        rows = [r for r in pool.map(lambda j: run_one(args.binary, args.outroot, j[0], j[1], args), jobs) if r]
    df = pd.DataFrame(rows)
    df.to_csv(os.path.join(args.outroot, "compare.csv"), index=False)
    pd.set_option("display.width", 200)
    summary = df.groupby("variant", sort=False)[COLUMNS].mean().round(3)
    print(summary.to_string())
    print("\nPDR per seed:")
    print(df.pivot(index="variant", columns="seed", values="pdr").loc[summary.index].round(3).to_string())


if __name__ == "__main__":
    main()
