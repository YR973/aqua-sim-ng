#!/usr/bin/env python3
"""Loss budget over many seeds (6 Nov 2026, request 2).

Usage: loss_budget.py BINARY OUTROOT [--seeds 101-120] [--jobs 4] [--extra "--NoiseBandOffsetDb=0"]
Runs P0 and ERGR-EMHC-g (round robin, ideal links, no shadowing, 0 m/s, N = 300) with 1 and 10 sources and averages
loss_budget.csv (hop causes, packet causes, airtime shares) and PDR over the seeds. Writes OUTROOT/loss_budget.md.
"""
import argparse, csv, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
CFG = os.path.join(HERE, "..", "configs")
SCEN = {"P0": "p0-published.txt", "G": "g-base.txt"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary"); ap.add_argument("outroot")
    ap.add_argument("--seeds", default="101-120")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--extra", default="")
    ap.add_argument("--protocols", default="P0,G")
    ap.add_argument("--sources", default="1,10")
    ap.add_argument("--ideal", default="true")
    args = ap.parse_args()
    lo, hi = (int(x) for x in args.seeds.split("-"))
    seeds = list(range(lo, hi + 1))
    jobs = [(p, int(s), seed) for p in args.protocols.split(",") for s in args.sources.split(",") for seed in seeds]

    def run(job):
        proto, ns, seed = job
        out = os.path.join(args.outroot, f"{proto}_S{ns}", f"seed{seed}")
        if not os.path.exists(os.path.join(out, "loss_budget.csv")):
            cmd = [args.binary, f"--ConfigFile={os.path.join(CFG, SCEN[proto])}", f"--NumSources={ns}", f"--Seed={seed}",
                   "--CurrentSpeed=0", f"--IdealLinks={args.ideal}", "--ShadowSigmaDb=0" if args.ideal == "true" else "--ShadowSigmaDb=5",
                   f"--OutDir={out}", "--LogLinks=false", "--LogNodes=false", "--LogEvents=false"] + args.extra.split()
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode:
                raise RuntimeError(r.stderr[-300:])
        rows = list(csv.DictReader(open(os.path.join(out, "loss_budget.csv"))))
        summ = next(csv.DictReader(open(os.path.join(out, "summary.csv"))))
        return job, rows, float(summ["pdr"])

    with ThreadPoolExecutor(args.jobs) as pool:
        res = list(pool.map(run, jobs))
    lines = []
    for proto in args.protocols.split(","):
        for ns in args.sources.split(","):
            sel = [(rows, pdr) for (p, s, seed), rows, pdr in res if p == proto and s == int(ns)]
            n = len(sel)
            agg = {}
            for rows, _ in sel:
                for r in rows:
                    agg.setdefault((r["group"], r["key"]), []).append(float(r["value"]))
            mean = lambda g, k: sum(agg.get((g, k), [0.0])) / n
            hops = mean("hop", "attempts")
            lines.append(f"### {proto}, {ns} source(s), {n} seeds: PDR {sum(p for _, p in sel) / n:.3f}, hop attempts per run {hops:.0f}, "
                         f"hop success {mean('hop', 'delivered') / max(hops, 1):.3f}")
            lines.append("")
            lines.append("| failed hops by cause | per run | % of all hops | % of failures |")
            lines.append("|---|---|---|---|")
            fails = hops - mean("hop", "delivered")
            for (g, k), v in sorted(agg.items()):
                if g == "hop" and k not in ("attempts", "delivered"):
                    m = sum(v) / n
                    lines.append(f"| {k} | {m:.1f} | {100 * m / max(hops, 1):.2f} | {100 * m / max(fails, 1e-9):.1f} |")
            resolved = mean("packet", "resolved")
            lines.append("")
            lines.append("| packets by outcome / loss cause | per run | % of resolved packets |")
            lines.append("|---|---|---|")
            for (g, k), v in sorted(agg.items()):
                if g == "packet" and k != "resolved":
                    m = sum(v) / n
                    lines.append(f"| {k} | {m:.1f} | {100 * m / max(resolved, 1):.2f} |")
            lines.append("")
            lines.append("| airtime by frame type | share of airtime | Erlang |")
            lines.append("|---|---|---|")
            for (g, k), v in sorted(agg.items()):
                if g == "airtime_share" and sum(v) > 0:
                    lines.append(f"| {k} | {100 * sum(v) / n:.1f}% | {mean('airtime_erlang', k):.4f} |")
            lines.append(f"| total | | {mean('airtime_erlang', 'total'):.4f} |")
            lines.append("")
    os.makedirs(args.outroot, exist_ok=True)
    open(os.path.join(args.outroot, "loss_budget.md"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
