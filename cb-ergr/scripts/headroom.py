#!/usr/bin/env python3
"""Headroom table: how much room is there above the static rule? (user item B8, 5 Nov 2026)

Usage: headroom.py BINARY OUTROOT [--seeds 106,107,108,109,110] [--base configs/g-base.txt] [--jobs 4]

For N in {100, 300}, Mac in {Csma, Aloha}, NumSources in {10, 30} it runs RoundRobin (RR), RR with hop ACKs (RR+FB),
FixedWeight with the physics weights (FW-phys), FixedWeight with the weights in configs/fw-tuned-v2.txt (FW-tuned, tuned at N = 100 on 5 Nov) and the
Oracle, and prints per scenario: oracle hop success, oracle PDR, and PDR gaps oracle - FW (best of the two FW rows),
oracle - RR and oracle - RR+FB. Only scenarios with an oracle - FW gap of roughly >= 0.1 are meaningful for the question
"does learning help". Writes OUTROOT/headroom.csv and OUTROOT/headroom.md.
"""
import os as _os_nr; _os_nr.environ.setdefault("CB_ERGR_NUMBERED_RUNS", "0")  # sweeps need fixed result paths (run_sweep, tune, ... read OutDir/summary.csv)
import argparse, csv, itertools, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
import statistics

HERE = os.path.dirname(os.path.abspath(__file__))


def fw_tuned_attr():
    for line in open(os.path.join(HERE, "..", "configs", "fw-tuned-v2.txt")):
        if line.startswith("SelectorAttributes"):
            return line.split("=", 1)[1].strip()
    return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("outroot")
    ap.add_argument("--seeds", default=",".join(str(s) for s in range(101, 121)))
    ap.add_argument("--base", default=os.path.join(HERE, "..", "configs", "g-base.txt"))
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--extra", default="", help="extra scenario arguments, e.g. '--NoiseBandOffsetDb=10'")
    args = ap.parse_args()
    seeds = [int(s) for s in args.seeds.split(",")]
    methods = {
        "RR": ("RoundRobin", ""),
        "RR+FB": ("RoundRobin", "WithFeedback=true"),
        "FW-phys": ("FixedWeight", ""),
        "FW-tuned": ("FixedWeight", fw_tuned_attr()),
        "Oracle": ("Oracle", ""),
        "Oracle+power": ("Oracle", "EnablePower=true"),
    }
    scenarios = list(itertools.product([100, 300], ["Csma", "Aloha"], [10, 30]))
    jobs = [(sc, m, s) for sc in scenarios for m in methods for s in seeds]

    def run(job):
        (n, mac, ns), m, seed = job
        out = os.path.join(args.outroot, f"N{n}_{mac}_S{ns}", m, f"seed{seed}")
        if not os.path.exists(os.path.join(out, "summary.csv")):
            sel, attrs = methods[m]
            cmd = [args.binary, f"--ConfigFile={args.base}", f"--N={n}", f"--Mac={mac}", f"--NumSources={ns}", f"--Seed={seed}",
                   f"--Selector={sel}", f"--OutDir={out}", "--LogLinks=false", "--LogPackets=false", "--LogNodes=false",
                   "--LogEvents=false"] + (["--SelectorAttributes=" + attrs] if attrs else []) + args.extra.split()
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                raise RuntimeError(r.stderr[-300:])
        row = next(csv.DictReader(open(os.path.join(out, "summary.csv"))))
        return job, float(row["pdr"]), float(row["hop_success_truth"]), float(row["energy_per_delivered_j"])

    with ThreadPoolExecutor(args.jobs) as pool:
        res = list(pool.map(run, jobs))
    mean = lambda xs: sum(xs) / len(xs)
    table = {}
    for (sc, m, s), pdr, hop, en in res:
        table.setdefault((sc, m), []).append((pdr, hop, en))
    T = {19: 2.093, 4: 2.776, 9: 2.262}
    def ci(xs):
        n = len(xs); m = sum(xs) / n
        sd = (sum((x - m) ** 2 for x in xs) / (n - 1)) ** 0.5
        return m, T.get(n - 1, 2.0) * sd / n ** 0.5
    order = ["RR", "RR+FB", "FW-phys", "FW-tuned", "Oracle", "Oracle+power"]
    lines = ["PDR (mean of %d seeds) and the paired gaps (95%% CI)" % len(seeds), "",
             "| N | MAC | sources | oracle hop success | " + " | ".join(order) + " | oracle - best FW | oracle - RR+FB | oracle - RR |",
             "|" + "---|" * (7 + len(order))]
    elines = ["Energy per delivered packet (J; transmit + receive + idle of all ordinary nodes)", "",
              "| N | MAC | sources | " + " | ".join(order) + " |", "|" + "---|" * (3 + len(order))]
    rows = []
    for sc in scenarios:
        n, mac, ns = sc
        pdr = {m: [p for p, h, e in table[(sc, m)]] for m in methods}
        en = {m: [e for p, h, e in table[(sc, m)]] for m in methods}
        hop = sum(h for p, h, e in table[(sc, "Oracle")]) / len(seeds)
        mean = lambda xs: sum(xs) / len(xs)
        bestfw = "FW-phys" if mean(pdr["FW-phys"]) >= mean(pdr["FW-tuned"]) else "FW-tuned"
        gaps = []
        for ref in (bestfw, "RR+FB", "RR"):
            g = ci([a - b for a, b in zip(pdr["Oracle"], pdr[ref])])
            gaps.append("%+.3f +- %.3f" % g)
        cells = ["%.3f" % mean(pdr[m]) for m in order]
        lines.append("| %d | %s | %d | %.3f | %s | %s |" % (n, mac, ns, hop, " | ".join(cells), " | ".join(gaps)))
        elines.append("| %d | %s | %d | %s |" % (n, mac, ns, " | ".join("%.1f" % mean(en[m]) for m in order)))
        rows.append([n, mac, ns, round(hop, 3)] + [round(mean(pdr[m]), 4) for m in order] + [round(mean(en[m]), 2) for m in order] + gaps)
    lines += ["", ""] + elines
    os.makedirs(args.outroot, exist_ok=True)
    with open(os.path.join(args.outroot, "headroom.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["N", "mac", "sources", "oracle_hop_success"] + ["pdr_" + m for m in order] + ["energy_" + m for m in order] + ["gap_oracle_bestfw", "gap_oracle_rrfb", "gap_oracle_rr"])
        w.writerows(rows)
    open(os.path.join(args.outroot, "headroom.md"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
