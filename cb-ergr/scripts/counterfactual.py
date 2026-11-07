#!/usr/bin/env python3
"""Counterfactual analysis of forwarder choice under hop ACK + retries (Step 2 of the 7 Nov 2026 request).

Usage: counterfactual.py BINARY OUTROOT [--seeds 101-120] [--jobs 4] [--methods FW,CB] [--cells 100x10,100x30,300x10,300x30]

Runs FW (physics weights) and CB (cold, defaults, AckMode=Every) with LogCounterfactual=true on configs/g-base.txt (Csma, calibrated
channel). Every DATA attempt then carries, from simulator-side ground truth, whether the very same frame would have been received at
each eligible candidate given what really happened at that candidate (R received; P below the PER draw; C collision with another
frame; H the candidate was transmitting; D the candidate was dead; O out of range), and what the FW shadow rule would have chosen.
No node reads any of this.

Reports (per cell and method, mean over seeds):
  1. failure causes of the chosen forwarder, as a share of routed DATA attempts (member hand-offs excluded);
  2. the time-split per-candidate oracle: per (sender, candidate) success (collisions included) estimated on decisions in the first half of the
     measured window, used on the second half: hop success of the chosen forwarder, of the FW shadow choice, of that oracle, and of the
     best candidate in hindsight (any candidate that would have received it);
  3. for CB: the share of decisions where CB's choice differs from FW's, and the outcome of both choices on those decisions.
Hop success here is per DATA transmission (retries included), not end-to-end PDR.
"""
import os as _os_nr; _os_nr.environ.setdefault("CB_ERGR_NUMBERED_RUNS", "0")  # sweeps need fixed result paths (run_sweep, tune, ... read OutDir/summary.csv)
import argparse, collections, csv, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
CFG = os.path.join(HERE, "..", "configs", "g-base.txt")
METHODS = {"FW": ("FixedWeight", ""), "CB": ("CbV5", "AckMode=Every")}
T95 = {19: 2.093, 9: 2.262, 4: 2.776}


def run_one(args, job):
    method, n, ns, seed = job
    out = os.path.join(args.outroot, f"N{n}_S{ns}", method, f"seed{seed}")
    if not os.path.exists(os.path.join(out, "counterfactual.csv")):
        sel, attrs = METHODS[method]
        cmd = [args.binary, f"--ConfigFile={CFG}", f"--N={n}", f"--NumSources={ns}", f"--Seed={seed}", f"--Selector={sel}",
               f"--OutDir={out}", "--LogCounterfactual=true", "--LogLinks=false", "--LogNodes=false", "--LogEvents=false",
               "--LogPackets=false"] + ([f"--SelectorAttributes={attrs}"] if attrs else [])
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr[-300:])
    return job, out


def read(out):
    rows = list(csv.DictReader(open(os.path.join(out, "counterfactual.csv"))))
    summ = next(csv.DictReader(open(os.path.join(out, "summary.csv"))))
    return rows, summ


def status_of(row, cand):
    for item in row["candidates"].split("|"):
        c, st = item.split(":")
        if int(c) == cand:
            return st
    return None


def analyse_run(rows, start, end):
    """Return counters for one run."""
    routed = [r for r in rows if r["shadow"] != "-2"]
    mid = 0.5 * (start + end)
    out = collections.Counter()
    # 1. causes of the chosen forwarder
    for r in routed:
        out["n"] += 1
        out["cause:" + r["chosen_cause"]] += 1
    # 2. time-split oracle
    stats = collections.defaultdict(lambda: [0, 0])  # (sender, cand) -> [n, received]
    for r in routed:
        if float(r["t"]) >= mid:
            continue
        for item in r["candidates"].split("|"):
            c, st = item.split(":")
            s = stats[(int(r["sender"]), int(c))]
            s[0] += 1
            s[1] += 1 if st == "R" else 0
    for r in routed:
        if float(r["t"]) < mid:
            continue
        cands = [(int(i.split(":")[0]), i.split(":")[1]) for i in r["candidates"].split("|")]
        out["n2"] += 1
        out["chosen_ok2"] += int(r["chosen_received"])
        sh = int(r["shadow"])
        if sh >= 0:
            out["shadow_ok2"] += 1 if status_of(r, sh) == "R" else 0
            out["shadow_n2"] += 1
        out["hindsight2"] += 1 if any(st == "R" for _, st in cands) else 0
        best, bestp = None, -1.0
        enough = True
        for c, st in cands:
            n, k = stats[(int(r["sender"]), c)]
            if n < 2:
                enough = False
            p = (k + 1.0) / (n + 2.0)
            if p > bestp + 1e-12:
                best, bestp = c, p
        out["oracle_ok2"] += 1 if dict(cands)[best] == "R" else 0
        out["enough2"] += 1 if enough else 0
        out["multi2"] += 1 if len(cands) >= 2 else 0
        # 3. disagreement with the shadow
        if sh >= 0:
            out["shadow_dec2"] += 1
            if sh != int(r["chosen"]):
                out["differ2"] += 1
                out["differ_chosen_ok2"] += int(r["chosen_received"])
                out["differ_shadow_ok2"] += 1 if status_of(r, sh) == "R" else 0
                out["differ_cause:" + r["chosen_cause"]] += 1
    return out


def ci(xs):
    n = len(xs)
    m = sum(xs) / n
    if n < 2:
        return m, 0.0
    sd = (sum((x - m) ** 2 for x in xs) / (n - 1)) ** 0.5
    return m, T95.get(n - 1, 2.0) * sd / n ** 0.5


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("outroot")
    ap.add_argument("--seeds", default="101-120")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--methods", default="FW,CB")
    ap.add_argument("--cells", default="100x10,100x30,300x10,300x30")
    args = ap.parse_args()
    lo, hi = (int(x) for x in args.seeds.split("-"))
    cells = [tuple(int(v) for v in c.split("x")) for c in args.cells.split(",")]
    jobs = [(m, n, ns, seed) for (n, ns) in cells for m in args.methods.split(",") for seed in range(lo, hi + 1)]
    with ThreadPoolExecutor(args.jobs) as pool:
        done = list(pool.map(lambda j: run_one(args, j), jobs))
    per = collections.defaultdict(list)   # (cell, method) -> [Counter per seed]
    pdr = collections.defaultdict(list)
    for (m, n, ns, seed), out in done:
        rows, summ = read(out)
        per[((n, ns), m)].append(analyse_run(rows, float(summ.get("warmup_s", 300) or 300), 2000.0))
        pdr[((n, ns), m)].append(float(summ["pdr"]))

    def rate(cs, num, den):
        xs = [c[num] / c[den] for c in cs if c[den] > 0]
        return ci(xs) if xs else (float("nan"), 0.0)

    lines = ["# Counterfactual analysis (hop ACK + retries, Csma, calibrated channel, seeds %d-%d)" % (lo, hi), ""]
    causes = ["ok", "per_loss", "collision_with_data", "collision_with_beacon_hello", "collision_with_ack_nack", "half_duplex",
              "receiver_dead", "out_of_range", "never_on_air", "unexplained_phy_busy"]
    lines += ["## 1. What happens to the DATA frame sent to the chosen forwarder (share of routed attempts, mean of seeds)", "",
              "| N | sources | method | PDR | attempts/run | " + " | ".join(causes) + " |", "|" + "---|" * (5 + len(causes))]
    for cell in cells:
        for m in args.methods.split(","):
            cs = per[(cell, m)]
            na = sum(c["n"] for c in cs) / len(cs)
            lines.append("| %d | %d | %s | %.3f | %.0f | %s |" % (cell[0], cell[1], m, sum(pdr[(cell, m)]) / len(cs), na,
                         " | ".join("%.3f" % (sum(c["cause:" + k] / max(1, c["n"]) for c in cs) / len(cs)) for k in causes)))
    lines += ["", "## 2. Learnable ceiling for the forwarder choice (second half of the window; hop success per DATA transmission, mean +- 95% CI over seeds)", "",
              "Time-split oracle: for every (sender, candidate) the success rate *including collisions* in the first half (Laplace 1/2 prior), then argmax over the "
              "candidates of each second-half decision. Hindsight = some candidate would have received the frame (upper bound, uses the outcome). "
              "'info' = share of second-half decisions where every candidate had >= 2 first-half samples.", "",
              "| N | sources | method | decisions/run (2nd half) | chosen | FW shadow | time-split oracle | hindsight best | oracle - chosen | info |",
              "|" + "---|" * 10]
    for cell in cells:
        for m in args.methods.split(","):
            cs = per[(cell, m)]
            ch = rate(cs, "chosen_ok2", "n2")
            sh = rate(cs, "shadow_ok2", "shadow_n2")
            orc = rate(cs, "oracle_ok2", "n2")
            hs = rate(cs, "hindsight2", "n2")
            gap = ci([c["oracle_ok2"] / c["n2"] - c["chosen_ok2"] / c["n2"] for c in cs if c["n2"]])
            info = rate(cs, "enough2", "n2")
            lines.append("| %d | %d | %s | %.0f | %.3f +- %.3f | %.3f +- %.3f | %.3f +- %.3f | %.3f +- %.3f | %+.3f +- %.3f | %.2f |" %
                         (cell[0], cell[1], m, sum(c["n2"] for c in cs) / len(cs), *ch, *sh, *orc, *hs, *gap, info[0]))
    if "CB" in args.methods.split(","):
        lines += ["", "## 3. CB against the FW shadow at the same decisions (second half)", "",
                  "| N | sources | decisions where CB != FW | hop success of CB's choice there | hop success FW's choice would have had | loss causes of CB's choice there |",
                  "|---|---|---|---|---|---|"]
        for cell in cells:
            cs = per[(cell, "CB")]
            frac = rate(cs, "differ2", "shadow_dec2")
            okc = rate(cs, "differ_chosen_ok2", "differ2")
            oks = rate(cs, "differ_shadow_ok2", "differ2")
            tot = collections.Counter()
            for c in cs:
                for k, v in c.items():
                    if k.startswith("differ_cause:"):
                        tot[k[13:]] += v
            s = sum(tot.values()) or 1
            lines.append("| %d | %d | %.3f +- %.3f | %.3f | %.3f | %s |" % (cell[0], cell[1], *frac, okc[0], oks[0],
                         ", ".join("%s %.2f" % (k, v / s) for k, v in tot.most_common(4))))
    os.makedirs(args.outroot, exist_ok=True)
    open(os.path.join(args.outroot, "counterfactual.md"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
