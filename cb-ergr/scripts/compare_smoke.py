#!/usr/bin/env python3
"""Run a few selector variants on several seeds in parallel and print a comparison table (kickoff D5).

Usage: compare_smoke.py BINARY OUTROOT [--rate 0.004] [--seeds 101,102,103] [--n 100] [--sim 2500]
Results of each run go to OUTROOT/<variant>_<seed>/; the table is also written to OUTROOT/compare.csv.
Training seeds (101-110) are the default so nothing here touches the test seeds 1-20.
"""
import os as _os_nr; _os_nr.environ.setdefault("CB_ERGR_NUMBERED_RUNS", "0")  # sweeps need fixed result paths (run_sweep, tune, ... read OutDir/summary.csv)
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
    "FW-phys": ("FixedWeight", ""),   # the hand-set physics weights (second baseline)
    "QL": ("QLearning", ""),
    "CARP": ("CarpEwma", ""),
    "LinQ": ("LfaQ", ""),
    "CB-pow": ("CbV5", "EnablePower=true", ["--RegretAllPowerLevels=true"]),
    "CB-pow-noLife": ("CbV5", "EnablePower=true;EnableLifetime=false", ["--RegretAllPowerLevels=true"]),
    "O": ("Oracle", ""),
    "GO": ("GeoOracle", ""),
}
def a1_attributes():
    """A1 weights written by fit_a1.py (configs/a1-weights.txt); empty if the file does not exist yet."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "configs", "a1-weights.txt")
    if not os.path.exists(path):
        return ""
    for line in open(path):
        if line.startswith("SelectorAttributes"):
            return line.split("=", 1)[1].strip()
    return ""


def config_attributes(name):
    """SelectorAttributes of configs/<name>.txt (written by scripts/tune.py); empty if the file does not exist yet."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "configs", name + ".txt")
    if not os.path.exists(path):
        return ""
    for line in open(path):
        if line.startswith("SelectorAttributes"):
            return line.split("=", 1)[1].strip()
    return ""


VARIANTS["A1"] = ("FittedLogistic", a1_attributes())
# FW = fixed weights TUNED on the training seeds with the same budget as CB-tuned (scripts/tune.py); FW-phys is the
# hand-set physics version. CB-tuned = CB with hyperparameters tuned the same way.
VARIANTS["FW"] = ("FixedWeight", config_attributes("fw-tuned"))
VARIANTS["CB-tuned"] = ("CbV5", config_attributes("cb-tuned"))
# Warm start: CB that begins at (and forgets toward) the A1 weights (design P7). a1_attributes() is "Weights=w1,...,w9".
VARIANTS["CB-warm"] = ("CbV5", a1_attributes().replace("Weights=", "PriorWeights="))
COLUMNS = ["pdr", "energy_tx_j", "regret_mean", "regret_mean_multi_viable", "share_multi_viable", "delay_mean_s", "attempts_total", "retx_per_delivered", "hop_success_truth", "ar_rate",
           "ack_frames_per_delivered", "label_ack_frac", "false_negative_rate",
           "ctrl_bytes_total_per_delivered", "energy_per_delivered_j", "energy_ack_tx_per_delivered_j", "energy_ack_rx_per_delivered_j"]


def run_one(binary, outroot, variant, seed, args):
    selector, attrs, *rest = VARIANTS[variant]
    extra = rest[0] if rest else []
    out = os.path.join(outroot, f"{variant}_{seed}")
    cmd = [binary, f"--Selector={selector}", f"--Seed={seed}", f"--N={args.n}", f"--SimTimeS={args.sim}",
           f"--WarmupS={args.warmup}", f"--PktRatePerNodeHz={args.rate}", f"--InitialEnergyJ={args.e0}",
           f"--CurrentSpeed={args.speed}", f"--OutDir={out}", "--LogLinks=false", "--LogNodes=false",
           "--LogPackets=false"]
    if attrs:
        cmd.append(f"--SelectorAttributes={attrs}")
    cmd.extend(extra)
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
