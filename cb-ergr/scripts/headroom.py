#!/usr/bin/env python3
"""Headroom table: how much room is there above the static rule? (user item B8, 5 Nov 2026)

Usage: headroom.py BINARY OUTROOT [--seeds 106,107,108,109,110] [--base configs/g-base.txt] [--jobs 4]

For N in {100, 300}, Mac in {Csma, Aloha}, NumSources in {10, 30} it runs RoundRobin (RR), RR with hop ACKs (RR+FB),
FixedWeight with the physics weights (FW-phys), FixedWeight with the weights in configs/fw-tuned.txt (FW-tuned) and the
Oracle, and prints per scenario: oracle hop success, oracle PDR, and PDR gaps oracle - FW (best of the two FW rows),
oracle - RR and oracle - RR+FB. Only scenarios with an oracle - FW gap of roughly >= 0.1 are meaningful for the question
"does learning help". Writes OUTROOT/headroom.csv and OUTROOT/headroom.md.
"""
import argparse, csv, itertools, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
import statistics

HERE = os.path.dirname(os.path.abspath(__file__))


def fw_tuned_attr():
    for line in open(os.path.join(HERE, "..", "configs", "fw-tuned.txt")):
        if line.startswith("SelectorAttributes"):
            return line.split("=", 1)[1].strip()
    return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("outroot")
    ap.add_argument("--seeds", default="106,107,108,109,110")
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
        return job, float(row["pdr"]), float(row["hop_success_truth"])

    with ThreadPoolExecutor(args.jobs) as pool:
        res = list(pool.map(run, jobs))
    mean = lambda xs: sum(xs) / len(xs)
    table = {}
    for (sc, m, s), pdr, hop in res:
        table.setdefault((sc, m), []).append((pdr, hop))
    lines = ["| N | MAC | sources | oracle hop success | oracle PDR | FW-phys | FW-tuned | RR+FB | RR | oracle - FW (best) | oracle - RR+FB | oracle - RR |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    rows = []
    for sc in scenarios:
        pdr = {m: mean([p for p, h in table[(sc, m)]]) for m in methods}
        hop = mean([h for p, h in table[(sc, "Oracle")]])
        # paired difference oracle - best FW per seed, to get a confidence half-width
        gap_best = max(pdr["FW-phys"], pdr["FW-tuned"])
        n, mac, ns = sc
        rows.append([n, mac, ns, round(hop, 3), round(pdr["Oracle"], 3)] + [round(pdr[m], 3) for m in ["FW-phys", "FW-tuned", "RR+FB", "RR"]] +
                    [round(pdr["Oracle"] - gap_best, 3), round(pdr["Oracle"] - pdr["RR+FB"], 3), round(pdr["Oracle"] - pdr["RR"], 3)])
        lines.append("| " + " | ".join(str(x) for x in rows[-1]) + " |")
    os.makedirs(args.outroot, exist_ok=True)
    with open(os.path.join(args.outroot, "headroom.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["N", "mac", "sources", "oracle_hop_success", "oracle_pdr", "fw_phys", "fw_tuned", "rr_fb", "rr", "gap_oracle_fw", "gap_oracle_rrfb", "gap_oracle_rr"])
        w.writerows(rows)
    open(os.path.join(args.outroot, "headroom.md"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
