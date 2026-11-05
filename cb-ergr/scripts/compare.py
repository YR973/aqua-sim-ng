#!/usr/bin/env python3
"""Compare selector variants on a scenario config (5 Nov 2026 benchmark).

Usage:
  compare.py BINARY OUTROOT --config configs/g-base.txt --extra "--N=100" --seeds 106,107,108,109,110 \
      --variant "RR=RoundRobin:" --variant "CB=CbV5:Lambda0=2.3;Gamma=0.95" ...
Each --variant is NAME=Selector:attributes (attributes as in --SelectorAttributes, may be empty).
Prints mean +- 95% CI (Student t) of PDR plus regret, AR rate, ACK frames per delivered packet, ACK energy and the hop success,
and writes OUTROOT/compare.csv (one row per run). Paired differences against --reference NAME are printed too.
"""
import argparse, csv, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

T = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228}
COLS = ["pdr", "hop_success_truth", "regret_mean", "ar_rate", "ack_frames_per_delivered", "energy_ack_tx_per_delivered_j",
        "energy_ack_rx_per_delivered_j", "energy_per_delivered_j", "mac_drops"]


def ci(xs):
    n = len(xs)
    m = sum(xs) / n
    if n < 2:
        return m, 0.0
    sd = (sum((x - m) ** 2 for x in xs) / (n - 1)) ** 0.5
    return m, T.get(n - 1, 2.0) * sd / n ** 0.5


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary"); ap.add_argument("outroot")
    ap.add_argument("--config", default=""); ap.add_argument("--extra", default="")
    ap.add_argument("--seeds", default="106,107,108,109,110")
    ap.add_argument("--variant", action="append", required=True)
    ap.add_argument("--reference", default="")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--decisions", action="store_true", help="write decisions.csv (needed for hop_windows.py)")
    args = ap.parse_args()
    seeds = [int(s) for s in args.seeds.split(",")]
    variants = {}
    for v in args.variant:
        name, rest = v.split("=", 1)
        sel, attrs = rest.split(":", 1)
        variants[name] = (sel, attrs)

    def run(job):
        name, seed = job
        sel, attrs = variants[name]
        out = os.path.join(args.outroot, name.replace("/", "_"), f"seed{seed}")
        if not os.path.exists(os.path.join(out, "summary.csv")):
            cmd = [args.binary] + ([f"--ConfigFile={args.config}"] if args.config else []) + \
                  [f"--Selector={sel}", f"--Seed={seed}", f"--OutDir={out}", "--LogLinks=false", "--LogNodes=false",
                   "--LogPackets=false", "--LogEvents=false"] + (["--LogDecisions=true"] if args.decisions else []) + args.extra.split()
            if attrs:
                cmd.append(f"--SelectorAttributes={attrs}")
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                raise RuntimeError(f"{name} seed {seed}: {r.stderr[-300:]}")
        row = next(csv.DictReader(open(os.path.join(out, "summary.csv"))))
        return name, seed, {c: float(row[c]) for c in COLS}

    with ThreadPoolExecutor(args.jobs) as pool:
        res = list(pool.map(run, [(n, s) for n in variants for s in seeds]))
    os.makedirs(args.outroot, exist_ok=True)
    with open(os.path.join(args.outroot, "compare.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(["variant", "seed"] + COLS)
        for n, s, d in res:
            w.writerow([n, s] + [d[c] for c in COLS])
    by = {n: {s: d for m, s, d in res if m == n} for n in variants}
    print("%-22s %-16s %7s %7s %7s %7s %7s" % ("variant", "PDR +- CI", "hop", "regret", "AR", "ACK/dlv", "ACK J"))
    for n in variants:
        m, h = ci([by[n][s]["pdr"] for s in seeds])
        mean = lambda c: sum(by[n][s][c] for s in seeds) / len(seeds)
        print("%-22s %.3f +- %.3f    %7.3f %7.3f %7.3f %7.2f %7.2f" % (n, m, h, mean("hop_success_truth"), mean("regret_mean"), mean("ar_rate"),
              mean("ack_frames_per_delivered"), mean("energy_ack_tx_per_delivered_j") + mean("energy_ack_rx_per_delivered_j")))
    if args.reference:
        print("\npaired PDR difference to", args.reference)
        for n in variants:
            if n == args.reference:
                continue
            m, h = ci([by[n][s]["pdr"] - by[args.reference][s]["pdr"] for s in seeds])
            print("  %-22s %+.3f +- %.3f" % (n, m, h))


if __name__ == "__main__":
    main()
