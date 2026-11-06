#!/usr/bin/env python3
"""Round 5 (9 Nov 2026): labels, ACK gates, Q-learning baselines, power levels.

  r5.py fit      BINARY OUTROOT   warm-start weights (x1..x10) from RR+FB decision logs of the BASE scenario with LabelRule=Evidence
  r5.py step1    BINARY OUTROOT   FW, FW-NA, CB-cold, CB-warm, CB-truth (diagnostic)
  r5.py step2    BINARY OUTROOT   CB-warm x AckGate {Every, Logit, ProbStd, ThompsonDisagree, BitmapOnly}, FW Every / BitmapOnly
  r5.py step3    BINARY OUTROOT   QL, QlLegacy, QELAR-G against FW and CB
  r5.py step4h   BINARY OUTROOT   power headroom: N300, 10 seeds, base
  r5.py step4    BINARY OUTROOT   CB-warm with power arms against FW-P, FW-LR, RR-P, Oracle+power (20 seeds, 4 cells, base + burst)
  r5.py life     BINARY OUTROOT   power-lifetime test with a 100 J battery (N300, 10 seeds, base)
Every run: configs/g-base.txt (Csma, calibrated channel) + configs/scn-<scenario>.txt, ChannelAwareBeacons=true, LabelRule=Evidence.
"""
import argparse, collections, csv, json, math, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import statlib
from chan import scn_args, summ, mean, series, period, recovery

CFG = os.path.join(HERE, "..", "configs")
GBASE = os.path.join(CFG, "g-base.txt")
WFILE = os.path.join(HERE, "..", "results", "r5", "fit_weights.json")
CELLS = [(100, 10), (100, 30), (300, 10), (300, 30)]
SEEDS20 = list(range(101, 121))


def warm():
    w = json.load(open(WFILE))["w10"] if os.path.exists(WFILE) else None
    return ",".join("%.6g" % x for x in w) if w else "0"


def methods():
    W = warm()
    cbw = "AckMode=Every;ChannelFeatures=true;NeighbourPrior=true;PriorWeights=" + W
    m = {
        "FW": ("FixedWeight", ""),
        "FW-NA": ("FixedWeight", "NoiseAware=true"),
        "FW-Bitmap": ("FixedWeight", "AckRequest=false"),
        "RR+FB": ("RoundRobin", "WithFeedback=true"),
        "CB-cold": ("CbV5", "AckMode=Every;ChannelFeatures=true"),
        "CB-warm": ("CbV5", cbw),
        "CB-truth": ("CbV5", "AckMode=Every;ChannelFeatures=true;TruthLabels=true"),
        "CB-warm-Logit": ("CbV5", cbw.replace("AckMode=Every", "AckMode=Gated;AckGate=Logit")),
        "CB-warm-ProbStd": ("CbV5", cbw.replace("AckMode=Every", "AckMode=Gated;AckGate=ProbStd")),
        "CB-warm-TD": ("CbV5", cbw.replace("AckMode=Every", "AckMode=Gated;AckGate=ThompsonDisagree")),
        "CB-warm-Bitmap": ("CbV5", cbw.replace("AckMode=Every", "AckMode=BitmapOnly")),
        "QL": ("QLearning", ""),
        "QlLegacy": ("QLearning", "Legacy=true"),
        "QELAR-G": ("Qelar", ""),
        "Oracle": ("Oracle", ""),
        "Oracle+P0": ("Oracle", "EnablePower=true;LambdaE=0"),
        "Oracle+P": ("Oracle", "EnablePower=true;LambdaE=0.25"),
        "Oracle+P1": ("Oracle", "EnablePower=true;LambdaE=1"),
        "FW-P": ("FixedWeight", "PowerRule=LowestFeasible"),
        "FW-LR": ("FixedWeight", "PowerRule=LowestReachChosen"),
        "RR-P": ("RoundRobin", "WithFeedback=true;LowestReach=true"),
        "CB-warm-P": ("CbV5", cbw + ";EnablePower=true"),
    }
    return m


def run(binary, outroot, method, scn, n, ns, seed, extra=(), decisions=False):
    sel, attrs = methods()[method]
    out = os.path.join(outroot, scn, f"N{n}_S{ns}", method, f"seed{seed}")
    if not os.path.exists(os.path.join(out, "summary.csv")):
        power = "EnablePower=true" in attrs or "PowerRule" in attrs or "LowestReach" in attrs
        cmd = [binary, f"--ConfigFile={GBASE}", f"--N={n}", f"--NumSources={ns}", f"--Seed={seed}", f"--Selector={sel}", f"--OutDir={out}",
               "--ChannelAwareBeacons=true", "--LabelRule=Evidence", "--LogLinks=false", "--LogNodes=false", "--LogEvents=false",
               "--LogPackets=true", "--ComputeRegret=true", f"--RegretAllPowerLevels={'true' if power else 'false'}",
               f"--LogThetaTrace={'true' if sel in ('CbV5', 'QLearning', 'Qelar') else 'false'}",
               f"--LogDecisions={'true' if decisions else 'false'}"] + scn_args(scn) + list(extra)
        if attrs:
            cmd.append(f"--SelectorAttributes={attrs}")
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(f"{out}: {r.stderr[-400:]}")
    return out


def runall(a, ms, scns, cells, seeds, extra=()):
    jobs = [(s, c, m, sd) for s in scns for c in cells for m in ms for sd in seeds]
    jobs.sort(key=lambda j: (-j[1][0], j[3]))
    def work(j):
        s, (n, ns), m, sd = j
        return j, run(a.binary, a.outroot, m, s, n, ns, sd, extra)
    with ThreadPoolExecutor(a.jobs) as p:
        return dict(p.map(work, jobs))


def lb(out):
    d = collections.Counter()
    for r in csv.DictReader(open(os.path.join(out, "loss_budget.csv"))):
        try:
            d[(r["group"], r["key"])] = float(r["value"])
        except ValueError:
            pass
    return d


def metrics(out):
    s = summ(out)
    L = lb(out)
    f = lambda k: float(s[k])
    att = max(1.0, L[("hop", "attempts")])
    delivered = max(1.0, f("packets_delivered"))
    window = f("sim_time_s") - f("warmup_s")
    airtime = sum(v for (g, k), v in L.items() if g == "airtime_erlang") * window / delivered
    labels = f("labels_total")
    cens = f("censored_labels")
    tot = max(1.0, labels + cens)
    share = lambda k: f(k) * labels / tot
    return {
        "pdr": f("pdr"), "e": f("energy_per_delivered_j"), "delay": f("delay_mean_s"), "delay95": f("delay_p95_s"),
        "ack": f("ack_frames_per_delivered"), "ctrl": f("ctrl_bytes_total_per_delivered"), "air": airtime, "ar": f("ar_rate"),
        "ldelay": f("label_delay_mean_s"), "hop": f("hop_success_truth"), "pos": f("label_positive_rate"), "fp": f("false_positive_rate"),
        "fn": f("false_negative_rate"), "s_ack": share("label_ack_frac"), "s_bmp": share("label_bitmap_frac"), "s_heard": share("label_heard_frac"),
        "s_cens": cens / tot, "s_void": share("label_void_frac"), "fnd": f("fnd_s"), "hnd": f("hnd_s"),
        "coll": L[("hop", "collision_with_data")] / att, "lock": L[("hop", "  of which: receiver locked by an earlier decodable DATA frame")] / att,
        "retx": f("retx_per_delivered"), "data_tx": f("data_tx_per_delivered"), "drop_maxtx": f("drop_max_tx_reached"),
        "beacon_bytes": f("ctrl_bytes_beacon_per_delivered"), "ack_bytes": f("ctrl_bytes_ack_per_delivered"),
    }


def regret(out, lo=300, hi=2000):
    r_o = r_f = n = 0.0
    for r in csv.DictReader(open(os.path.join(out, "regret_by_time.csv"))):
        b = int(r["bin_start_s"])
        if lo <= b < hi:
            d = int(r["decisions"])
            r_o += float(r["regret_vs_oracle"]) * d
            r_f += float(r["regret_vs_fw"]) * d
            n += d
    return (r_o / n, r_f / n) if n else (float("nan"), float("nan"))


def theta_rows(out):
    p = os.path.join(out, "theta_trace.csv")
    return list(csv.DictReader(open(p))) if os.path.exists(p) else []


def pdr_tables(L, res, ms, refs, scn, cells, seeds):
    L += ["### PDR, scenario %s (mean of %d seeds)" % (scn, len(seeds)), "", "| method | " + " | ".join("N=%d,%d" % c for c in cells) + " | mean |", "|" + "---|" * (len(cells) + 2)]
    for m in ms:
        v = [mean(metrics(res[(scn, c, m, sd)])["pdr"] for sd in seeds) for c in cells]
        L.append("| %s | %s | %.3f |" % (m, " | ".join("%.3f" % x for x in v), mean(v)))
    for ref in refs:
        L += ["", "Paired difference to %s (95%% CI; Holm p over the cells; EQ = TOST +-0.02 after Holm):" % ref, "",
              "| method | " + " | ".join("N=%d,%d" % c for c in cells) + " |", "|" + "---|" * (len(cells) + 1)]
        for m in ms:
            if m == ref:
                continue
            d = [[metrics(res[(scn, c, m, sd)])["pdr"] - metrics(res[(scn, c, ref, sd)])["pdr"] for sd in seeds] for c in cells]
            adj = statlib.holm([statlib.paired_t_p(x) for x in d])
            tadj = statlib.holm([statlib.tost_p(x, 0.02) for x in d])
            L.append("| %s | %s |" % (m, " | ".join("%+.3f +- %.3f (p=%.3f)%s" % (*statlib.mean_ci(x), p, " EQ" if t < 0.05 else "") for x, p, t in zip(d, adj, tadj))))
    L.append("")


def cost_table(L, res, ms, scn, cells, seeds, keys):
    names = {"pdr": "PDR", "e": "J/pkt", "delay": "delay s", "ack": "ACK frames/pkt", "ctrl": "ctrl B/pkt", "air": "airtime s/pkt", "ar": "ACK-request rate",
             "ldelay": "label delay s", "coll": "DATA collision share", "lock": "receiver-lock share", "fnd": "first death s", "hnd": "half dead s",
             "retx": "retx/pkt", "data_tx": "DATA tx/pkt", "beacon_bytes": "beacon B/pkt", "ack_bytes": "ACK B/pkt", "drop_maxtx": "MaxTx drops/run",
             "hop": "hop success (truth)", "regret": "regret vs oracle", "regret_fw": "regret vs FW"}
    L += ["Mean over the %d runs (scenario %s, all cells):" % (len(seeds) * len(cells), scn), "", "| method | " + " | ".join(names[k] for k in keys) + " |", "|" + "---|" * (len(keys) + 1)]
    for m in ms:
        rows = [res[(scn, c, m, sd)] for c in cells for sd in seeds]
        vals = []
        for k in keys:
            if k in ("regret", "regret_fw"):
                vals.append("%.3f" % mean(regret(o)[0 if k == "regret" else 1] for o in rows))
            elif k in ("fnd", "hnd"):
                xs = [metrics(o)[k] for o in rows]
                dead = [x for x in xs if x > 0]
                vals.append(("%.0f [%d/%d]" % (mean(dead), len(dead), len(xs))) if dead else "- [0/%d]" % len(xs))
            else:
                vals.append("%.3f" % mean(metrics(o)[k] for o in rows) if k in ("pdr", "ar", "coll", "lock", "hop") else "%.2f" % mean(metrics(o)[k] for o in rows))
        L.append("| %s | %s |" % (m, " | ".join(vals)))
    L.append("")


def theta_table(L, res, ms, scn, cells, seeds, cols):
    L += ["Learned parameters over time (mean over the heads that have labels, then over seeds; N=300 cells), scenario %s:" % scn, "",
          "| method | parameter | " + " | ".join("t=%d" % t for t in (500, 800, 1100, 1400, 1700, 2000)) + " |", "|" + "---|" * 8]
    for m in ms:
        for idx, name in cols.get(m, []):
            row = []
            for t in (500, 800, 1100, 1400, 1700, 2000):
                v = []
                for c in cells:
                    if c[0] != 300:
                        continue
                    for sd in seeds:
                        for r in theta_rows(res[(scn, c, m, sd)]):
                            if abs(float(r["t_s"]) - t) < 1 and float(r["heads_with_labels"]) > 0:
                                v.append(float(r["theta%d" % (idx + 1)]))
                row.append("%.3f" % mean(v) if v else "-")
            L.append("| %s | %s | %s |" % (m, name, " | ".join(row)))
    L.append("")


def write(a, name, L):
    text = "\n".join(L) + "\n"
    open(os.path.join(a.outroot, name), "w").write(text)
    print(text)


# ------------------------------------------------------------------------------------------------------------------------------------
def fit(a):
    import numpy as np
    jobs = [(c, sd) for c in [(300, 10), (300, 30), (100, 30)] for sd in range(101, 111)]
    with ThreadPoolExecutor(a.jobs) as p:
        outs = list(p.map(lambda j: run(a.binary, a.outroot, "RR+FB", "base", j[0][0], j[0][1], j[1], decisions=True), jobs))
    X, y = [], []
    for out in outs:
        for r in csv.DictReader(open(os.path.join(out, "decisions.csv"))):
            if r["learn"] == "1" and r["label"] in ("0", "1"):
                X.append([float(r[f"x{i}"]) for i in range(1, 11)])
                y.append(float(r["label"]))
    X, y = np.array(X), np.array(y)
    sig = lambda z: 1.0 / (1.0 + np.exp(-np.clip(z, -30, 30)))
    w = np.zeros(10)
    for _ in range(60):
        p = sig(X @ w)
        step = np.linalg.solve((X * (p * (1 - p))[:, None]).T @ X + np.eye(10), X.T @ (p - y) + w)
        w -= step
        if abs(step).max() < 1e-9:
            break
    p = np.clip(sig(X @ w), 1e-12, 1 - 1e-12)
    info = {"rows": int(len(y)), "positive_rate": float(y.mean()), "logloss": float(-np.mean(y * np.log(p) + (1 - y) * np.log(1 - p))),
            "w10": [float(v) for v in w], "fit_seeds": "101-110", "cells": "N300/10, N300/30, N100/30", "scenario": "base", "labels": "LabelRule=Evidence, RR+FB"}
    os.makedirs(os.path.dirname(WFILE), exist_ok=True)
    json.dump(info, open(WFILE, "w"), indent=1)
    print(json.dumps(info, indent=1))


def step1(a):
    ms = ["FW", "FW-NA", "CB-cold", "CB-warm", "CB-truth"]
    scns = ["base", "burst"]
    res = runall(a, ms, scns, CELLS, SEEDS20)
    L = ["# Step 1: labels (LabelRule=Evidence), 20 seeds, paired", ""]
    for scn in scns:
        L += ["## Scenario %s" % scn, ""]
        pdr_tables(L, res, ms, ["FW", "FW-NA"], scn, CELLS, SEEDS20)
        cost_table(L, res, ms, scn, CELLS, SEEDS20, ["pdr", "e", "delay", "ack", "regret", "regret_fw"])
        L += ["Labels per cell (mean of seeds): positive rate of the labels against the ground-truth hop success; where the attempts' outcomes came from "
              "(ACK, (a) receipt bitmap, (b) heard, (c) censored, void NACK); false-positive / false-negative rates of the labels against ground truth:", "",
              "| cell | method | label positive rate | hop success (truth) | ACK | (a) bitmap | (b) heard | (c) censored | void | FP rate | FN rate | label delay s |", "|" + "---|" * 12]
        for c in CELLS:
            for m in ms:
                M = [metrics(res[(scn, c, m, sd)]) for sd in SEEDS20]
                g = lambda k: mean(x[k] for x in M)
                L.append("| N=%d,%d | %s | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f | %.4f | %.3f | %.1f |" % (c[0], c[1], m, g("pos"), g("hop"), g("s_ack"), g("s_bmp"), g("s_heard"), g("s_cens"), g("s_void"), g("fp"), g("fn"), g("ldelay")))
        L.append("")
        theta_table(L, res, ms, scn, CELLS, SEEDS20, {m: [(7, "x8"), (9, "x10"), (8, "x9 intercept")] for m in ("CB-cold", "CB-warm", "CB-truth")})
    write(a, "step1.md", L)


def step2(a):
    ms = ["CB-warm", "CB-warm-Logit", "CB-warm-ProbStd", "CB-warm-TD", "CB-warm-Bitmap", "FW", "FW-Bitmap"]
    scns = ["base", "burst"]
    res = runall(a, ms, scns, CELLS, SEEDS20)
    L = ["# Step 2: ACK gates with LabelRule=Evidence (CB-warm = AckMode Every), 20 seeds, paired", ""]
    for scn in scns:
        L += ["## Scenario %s" % scn, ""]
        pdr_tables(L, res, ms, ["CB-warm", "FW"], scn, CELLS, SEEDS20)
        cost_table(L, res, ms, scn, CELLS, SEEDS20, ["pdr", "ack", "ack_bytes", "ctrl", "air", "e", "ar", "ldelay", "delay", "retx"])
    # why TD lost at 30 sources: per-cell diagnostics
    L += ["## Diagnosis of the ThompsonDisagree gate at 30 sources (base, N=300/30 and N=100/30; mean of seeds)", "",
          "| cell | method | PDR | ACK-request rate | label delay s | DATA tx/pkt | retx/pkt | MaxTx drops/run | delay s | (b) heard | (c) censored | ACK share of outcomes | labels/run |", "|" + "---|" * 13]
    for c in [(100, 30), (300, 30), (300, 10)]:
        for m in ["CB-warm", "CB-warm-TD", "CB-warm-Bitmap"]:
            M = [metrics(res[("base", c, m, sd)]) for sd in SEEDS20]
            labs = mean(float(summ(res[("base", c, m, sd)])["labels_total"]) for sd in SEEDS20)
            g = lambda k: mean(x[k] for x in M)
            L.append("| N=%d,%d | %s | %.3f | %.2f | %.1f | %.2f | %.2f | %.1f | %.1f | %.3f | %.3f | %.3f | %.0f |" % (c[0], c[1], m, g("pdr"), g("ar"), g("ldelay"), g("data_tx"), g("retx"), g("drop_maxtx"), g("delay"), g("s_heard"), g("s_cens"), g("s_ack"), labs))
    write(a, "step2.md", L)


def step3(a):
    ms = ["FW", "CB-warm", "QL", "QlLegacy", "QELAR-G"]
    scns = ["base", "burst"]
    res = runall(a, ms, scns, CELLS, SEEDS20)
    L = ["# Step 3: Q-learning methods, 20 seeds, paired", ""]
    for scn in scns:
        L += ["## Scenario %s" % scn, ""]
        pdr_tables(L, res, ms, ["FW", "CB-warm"], scn, CELLS, SEEDS20)
        cost_table(L, res, ms, scn, CELLS, SEEDS20, ["pdr", "e", "delay", "ack", "regret", "regret_fw", "beacon_bytes", "ack_bytes", "ctrl"])
        theta_table(L, res, ms, scn, CELLS, SEEDS20, {"QL": [(0, "V of the head"), (1, "mean learned Q")], "QlLegacy": [(0, "V of the head"), (1, "mean learned Q")],
                                                     "QELAR-G": [(0, "V of the head"), (1, "mean estimated P")]})
    write(a, "step3.md", L)


def step4h(a):
    ms = ["Oracle", "Oracle+P0", "Oracle+P", "Oracle+P1", "FW", "FW-P", "FW-LR", "RR+FB", "RR-P"]
    cells = [(300, 10), (300, 30)]
    seeds = list(range(101, 111))
    res = runall(a, ms, ["base"], cells, seeds)
    L = ["# Step 4 headroom: power levels (base, N=300, 10 seeds)", ""]
    pdr_tables(L, res, ms, ["Oracle", "FW"], "base", cells, seeds)
    for c in cells:
        cost_table(L, {k: v for k, v in res.items() if k[1] == c}, ms, "base", [c], seeds, ["pdr", "e", "coll", "lock", "fnd", "hnd", "data_tx"])
    write(a, "step4h.md", L)


def step4(a):
    ms = ["FW", "FW-P", "FW-LR", "RR-P", "CB-warm", "CB-warm-P", "Oracle+P"]
    scns = ["base", "burst"]
    res = runall(a, ms, scns, CELLS, SEEDS20)
    L = ["# Step 4: power levels, 20 seeds, paired", ""]
    for scn in scns:
        L += ["## Scenario %s" % scn, ""]
        pdr_tables(L, res, ms, ["FW-P", "FW-LR"], scn, CELLS, SEEDS20)
        cost_table(L, res, ms, scn, CELLS, SEEDS20, ["pdr", "e", "fnd", "hnd", "coll", "lock", "regret"])
        # energy paired differences
        L += ["Energy per delivered packet, paired difference CB-warm-P - FW-P and - FW-LR (J, 95% CI):", "", "| reference | " + " | ".join("N=%d,%d" % c for c in CELLS) + " |", "|---|---|---|---|---|"]
        for ref in ("FW-P", "FW-LR"):
            L.append("| %s | %s |" % (ref, " | ".join("%+.2f +- %.2f" % statlib.mean_ci([metrics(res[(scn, c, "CB-warm-P", sd)])["e"] - metrics(res[(scn, c, ref, sd)])["e"] for sd in SEEDS20]) for c in CELLS)))
        L.append("")
    write(a, "step4.md", L)


def life(a):
    ms = ["FW", "FW-P", "FW-LR", "RR-P", "CB-warm", "CB-warm-P", "Oracle", "Oracle+P"]
    cells = [(300, 10), (300, 30)]
    seeds = list(range(101, 111))
    a.outroot = os.path.join(a.outroot, "life100J")   # its own run cache: the battery is not part of the run path
    os.makedirs(a.outroot, exist_ok=True)
    res = runall(a, ms, ["base"], cells, seeds, extra=("--InitialEnergyJ=100",))
    L = ["# Step 4 power-lifetime test: 100 J battery (instead of 300 J), base, N=300, 10 seeds", ""]
    pdr_tables(L, res, ms, ["FW", "FW-P"], "base", cells, seeds)
    for c in cells:
        cost_table(L, {k: v for k, v in res.items() if k[1] == c}, ms, "base", [c], seeds, ["pdr", "e", "fnd", "hnd", "coll"])
    # delivered packets per run (lifetime matters for the total)
    L += ["Delivered packets per run (mean):", "", "| method | " + " | ".join("N=%d,%d" % c for c in cells) + " |", "|---|---|---|"]
    for m in ms:
        L.append("| %s | %s |" % (m, " | ".join("%.0f" % mean(float(summ(res[("base", c, m, sd)])["packets_delivered"]) for sd in seeds) for c in cells)))
    write(a, "life.md", L)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("part", choices=["fit", "step1", "step2", "step3", "step4h", "step4", "life"])
    ap.add_argument("binary")
    ap.add_argument("outroot")
    ap.add_argument("--jobs", type=int, default=4)
    x = ap.parse_args()
    os.makedirs(x.outroot, exist_ok=True)
    {"fit": fit, "step1": step1, "step2": step2, "step3": step3, "step4h": step4h, "step4": step4, "life": life}[x.part](x)
