#!/usr/bin/env python3
"""Steps A-D of the 8 Nov 2026 request: channel-aware beacons, literature scenarios, headroom, comparison.

  chan.py headroom BINARY OUTROOT          Step C: per scenario (base, burst, shipping, col10), N300/10 and N300/30, 10 seeds
  chan.py fit      BINARY OUTROOT          warm-start weights for CB, fitted on the BASE scenario only (RR+FB decision logs, training seeds)
  chan.py compare  BINARY OUTROOT          Step D: 20 seeds, 4 cells, scenarios base / burst / shipping
All runs use configs/g-base.txt (paper settings, Csma, calibrated channel) + configs/scn-<name>.txt + ChannelAwareBeacons=true (the same
beacon load for every method; FW0 = FW without the channel fields is run in the base scenario to measure the beacon cost).
"""
import argparse, collections, csv, json, math, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import statlib

CFG = os.path.join(HERE, "..", "configs")
GBASE = os.path.join(CFG, "g-base.txt")
PHYS9 = [-0.4, 0.5, 0.8, 0.2, 0.1, -0.2, 0.2, 9.0, -3.0]


def scn_args(name):
    if name == "base":
        return []
    out = []
    for line in open(os.path.join(CFG, f"scn-{name}.txt")):
        line = line.strip()
        if line and not line.startswith("#"):
            k, v = [t.strip() for t in line.split("=", 1)]
            out.append(f"--{k}={v}")
    return out


def weights():
    p = os.path.join(HERE, "..", "results", "chan", "fit_weights.json")
    return json.load(open(p)) if os.path.exists(p) else None


def wstr(w):
    return ",".join("%.6g" % x for x in w)


def methods():
    m = collections.OrderedDict()
    m["Oracle"] = ("Oracle", "", True)
    m["FW"] = ("FixedWeight", "", True)
    m["FW0"] = ("FixedWeight", "", False)          # FW without the channel fields on the beacons
    m["FW-NA"] = ("FixedWeight", "NoiseAware=true", True)
    m["RR+FB"] = ("RoundRobin", "WithFeedback=true", True)
    m["CB-cold"] = ("CbV5", "AckMode=Every;ChannelFeatures=true", True)
    w = weights()
    if w:
        m["CB-warm"] = ("CbV5", "AckMode=Every;ChannelFeatures=true;NeighbourPrior=true;PriorWeights=" + wstr(w["w11"]), True)
        m["CB-warm-old"] = ("CbV5", "AckMode=Every;NeighbourPrior=true;PriorWeights=" + wstr(w["w9"]), True)
    na = PHYS9[:7] + [0.0, PHYS9[8], PHYS9[7], 0.0]   # FW-NA as a warm start: weight 9.0 on x10, none on x8
    m["CB-warm-NA"] = ("CbV5", "AckMode=Every;ChannelFeatures=true;NeighbourPrior=true;PriorWeights=" + wstr(na), True)
    return m


def run(binary, outroot, method, scn, n, ns, seed, extra=(), packets=False, cf=False, regret=False, theta=False, decisions=False):
    sel, attrs, beacons = methods()[method]
    out = os.path.join(outroot, scn, f"N{n}_S{ns}", method, f"seed{seed}")
    if not os.path.exists(os.path.join(out, "summary.csv")):
        cmd = [binary, f"--ConfigFile={GBASE}", f"--N={n}", f"--NumSources={ns}", f"--Seed={seed}", f"--Selector={sel}", f"--OutDir={out}",
               f"--ChannelAwareBeacons={'true' if beacons else 'false'}", "--LogLinks=false", "--LogNodes=false", "--LogEvents=false",
               f"--LogPackets={'true' if packets else 'false'}", f"--LogCounterfactual={'true' if cf else 'false'}",
               f"--ComputeRegret={'true' if regret else 'false'}", f"--LogThetaTrace={'true' if theta else 'false'}",
               f"--LogDecisions={'true' if decisions else 'false'}"] + scn_args(scn) + list(extra)
        if attrs:
            cmd.append(f"--SelectorAttributes={attrs}")
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(f"{out}: {r.stderr[-300:]}")
    return out


def summ(out):
    return next(csv.DictReader(open(os.path.join(out, "summary.csv"))))


def mean(xs):
    xs = list(xs)
    return sum(xs) / len(xs) if xs else float("nan")


def pool(jobs, fn, nthreads):
    with ThreadPoolExecutor(nthreads) as p:
        return list(p.map(fn, jobs))


# ----------------------------------------------------------------------------------------------------------------- counterfactual window
def cf_window(rows, a, b):
    """Time-split per-candidate oracle in [a, b): estimate on the first half of the window, evaluate on the second half."""
    mid = 0.5 * (a + b)
    stats = collections.defaultdict(lambda: [0, 0])
    for r in rows:
        if r["shadow"] == "-2":
            continue
        t = float(r["t"])
        if a <= t < mid:
            for item in r["candidates"].split("|"):
                c, st = item.split(":")
                s = stats[(int(r["sender"]), int(c))]
                s[0] += 1
                s[1] += 1 if st == "R" else 0
    n = chosen = oracle = 0
    for r in rows:
        if r["shadow"] == "-2":
            continue
        t = float(r["t"])
        if not (mid <= t < b):
            continue
        cands = [(int(i.split(":")[0]), i.split(":")[1]) for i in r["candidates"].split("|")]
        best, bp = None, -1.0
        for c, st in cands:
            k, m = stats[(int(r["sender"]), c)]
            p = (m + 1.0) / (k + 2.0)
            if p > bp + 1e-12:
                best, bp = c, p
        n += 1
        chosen += int(r["chosen_received"])
        oracle += 1 if dict(cands)[best] == "R" else 0
    return n, chosen, oracle


def ci(xs):
    return statlib.mean_ci(xs) if len(xs) > 1 else (xs[0], 0.0)


# ----------------------------------------------------------------------------------------------------------------- Step C
def headroom(a):
    scns = ["base", "burst", "shipping", "col10"]
    cells = [(300, 10), (300, 30)]
    seeds = list(range(101, 111))
    ms = ["Oracle", "FW", "FW-NA"]
    jobs = [(s, c, m, sd) for s in scns for c in cells for m in ms for sd in seeds]
    jobs += [("base", c, "FW0", sd) for c in cells for sd in seeds]
    jobs.sort(key=lambda j: j[3])

    def work(j):
        s, (n, ns), m, sd = j
        return j, run(a.binary, a.outroot, m, s, n, ns, sd, cf=(m == "FW"))

    res = dict(pool(jobs, work, a.jobs))
    L = ["# Step C: headroom per scenario (N = 300, Csma, 10 seeds 101-110, calibrated channel, ChannelAwareBeacons on for every row except FW0)", "",
         "Scenarios: base; burst = rain squall (7.3 dB, 800-1400 s, central 2x2 cells, upper 1500 m); shipping = lane (+0.0013 dB, literature value); col10 = the +10 dB cell column.", "",
         "| scenario | cell | PDR Oracle | PDR FW | PDR FW-NA | Oracle - FW | FW-NA - FW | time-split CF oracle - FW (hop success, whole run) | CF oracle - FW inside 800-1400 s | CF decisions in 800-1400 s per run |", "|" + "---|" * 10]
    for s in scns:
        for (n, ns) in cells:
            g = lambda m: [float(summ(res[(s, (n, ns), m, sd)])["pdr"]) for sd in seeds]
            o, f, na = g("Oracle"), g("FW"), g("FW-NA")
            wins = {"all": (300.0, 2000.0), "burst": (800.0, 1400.0)}
            cfd = {}
            for k, (lo, hi) in wins.items():
                diffs, cnt = [], []
                for sd in seeds:
                    rows = list(csv.DictReader(open(os.path.join(res[(s, (n, ns), "FW", sd)], "counterfactual.csv"))))
                    nn, ch, orc = cf_window(rows, lo, hi)
                    if nn:
                        diffs.append((orc - ch) / nn)
                    cnt.append(nn)
                cfd[k] = (ci(diffs), mean(cnt))
            L.append("| %s | N=%d, %d src | %.3f | %.3f | %.3f | %+.3f +- %.3f | %+.3f +- %.3f | %+.3f +- %.3f | %+.3f +- %.3f | %.0f |" %
                     (s, n, ns, mean(o), mean(f), mean(na), *ci([x - y for x, y in zip(o, f)]), *ci([x - y for x, y in zip(na, f)]), *cfd["all"][0], *cfd["burst"][0], cfd["burst"][1]))
    # beacon cost (Step A5)
    L += ["", "## Beacon cost of the channel fields (base scenario; FW0 = without, FW = with)", "",
          "| cell | beacon bytes per delivered packet FW0 | FW | beacon airtime share FW0 | FW | PDR FW0 | FW |", "|---|---|---|---|---|---|---|"]
    for (n, ns) in cells:
        b = {}
        for m in ("FW0", "FW"):
            bb, sh, pdr = [], [], []
            for sd in seeds:
                out = res[("base", (n, ns), m, sd)]
                sm = summ(out)
                bb.append(float(sm["ctrl_bytes_beacon_per_delivered"]))
                pdr.append(float(sm["pdr"]))
                for r in csv.DictReader(open(os.path.join(out, "loss_budget.csv"))):
                    if r["group"] == "airtime_share" and r["key"] == "beacon":
                        sh.append(float(r["value"]))
            b[m] = (mean(bb), mean(sh), mean(pdr))
        L.append("| N=%d, %d src | %.1f | %.1f | %.4f | %.4f | %.3f | %.3f |" % (n, ns, b["FW0"][0], b["FW"][0], b["FW0"][1], b["FW"][1], b["FW0"][2], b["FW"][2]))
    text = "\n".join(L) + "\n"
    open(os.path.join(a.outroot, "headroom.md"), "w").write(text)
    print(text)


# ----------------------------------------------------------------------------------------------------------------- fit
def fit(a):
    import numpy as np
    seeds = list(range(101, 111))
    cells = [(300, 10), (300, 30), (100, 30)]
    jobs = [(c, sd) for c in cells for sd in seeds]
    outs = pool(jobs, lambda j: run(a.binary, a.outroot, "RR+FB", "base", j[0][0], j[0][1], j[1], decisions=True), a.jobs)
    X, y = [], []
    for out in outs:
        for r in csv.DictReader(open(os.path.join(out, "decisions.csv"))):
            if r["learn"] != "1" or r["label"] not in ("0", "1"):
                continue
            X.append([float(r[f"x{i}"]) for i in range(1, 12)])
            y.append(float(r["label"]))
    X, y = np.array(X), np.array(y)

    def sig(z):
        return 1.0 / (1.0 + np.exp(-np.clip(z, -30, 30)))

    def newton(Xs):
        w = np.zeros(Xs.shape[1])
        for _ in range(60):
            p = sig(Xs @ w)
            g = Xs.T @ (p - y) + 1.0 * w
            H = (Xs * (p * (1 - p))[:, None]).T @ Xs + np.eye(Xs.shape[1])
            step = np.linalg.solve(H, g)
            w -= step
            if abs(step).max() < 1e-9:
                break
        return w

    w9 = newton(X[:, :9])
    w11 = newton(X)
    base_rate = y.mean()
    def ll(Xs, w):
        p = np.clip(sig(Xs @ w), 1e-12, 1 - 1e-12)
        return float(-np.mean(y * np.log(p) + (1 - y) * np.log(1 - p)))
    info = {"rows": int(len(y)), "positive_rate": float(base_rate), "logloss_9": ll(X[:, :9], w9), "logloss_11": ll(X, w11),
            "w9": [float(v) for v in w9], "w11": [float(v) for v in w11], "fit_seeds": "101-110", "cells": [list(c) for c in cells], "scenario": "base, RR+FB decision logs"}
    os.makedirs(os.path.join(HERE, "..", "results", "chan"), exist_ok=True)
    json.dump(info, open(os.path.join(HERE, "..", "results", "chan", "fit_weights.json"), "w"), indent=1)
    print(json.dumps(info, indent=1))


# ----------------------------------------------------------------------------------------------------------------- Step D
def series(packet_lists, t0, t1, width=100.0, step=25.0):
    out = []
    t = t0
    while t + width <= t1 + 1e-9:
        created = delivered = 0
        for pk in packet_lists:
            for c, d in pk:
                if t <= c < t + width:
                    created += 1
                    delivered += 1 if d else 0
        out.append((t, delivered / created if created else float("nan")))
        t += step
    return out


def period(pk_lists, lo, hi):
    created = delivered = 0
    for pk in pk_lists:
        for c, d in pk:
            if lo <= c < hi:
                created += 1
                delivered += 1 if d else 0
    return delivered / created if created else float("nan")


def recovery(ser, ref, base, tol=0.02, need=2):
    for i, (t, p) in enumerate(ser):
        if t < ref:
            continue
        w = ser[i:i + need]
        if len(w) == need and all((not math.isnan(q)) and q >= base - tol for _, q in w):
            return t - ref
    return None


def compare(a):
    scns = a.scenarios.split(",")
    cells = [(100, 10), (100, 30), (300, 10), (300, 30)]
    seeds = list(range(101, 101 + a.nseeds))
    ms = a.methods.split(",")
    jobs = [(s, c, m, sd) for s in scns for c in cells for m in ms for sd in seeds]
    jobs.sort(key=lambda j: (scns.index(j[0]), -j[1][0]))

    def work(j):
        s, (n, ns), m, sd = j
        return j, run(a.binary, a.outroot, m, s, n, ns, sd, packets=True, regret=True, theta=m.startswith("CB"))

    res = dict(pool(jobs, work, a.jobs))
    L = ["# Step D: comparison (Csma, calibrated channel, %d seeds %d-%d, paired; ChannelAwareBeacons on for every method)" % (len(seeds), seeds[0], seeds[-1]), ""]
    S = lambda s, c, m, sd: summ(res[(s, c, m, sd)])
    for s in scns:
        L += ["## Scenario: %s" % s, ""]
        L += ["| method | " + " | ".join("N=%d,%d src" % c for c in cells) + " | mean |", "|" + "---|" * (len(cells) + 2)]
        for m in ms:
            v = [mean(float(S(s, c, m, sd)["pdr"]) for sd in seeds) for c in cells]
            L.append("| %s | %s | %.3f |" % (m, " | ".join("%.3f" % x for x in v), mean(v)))
        for ref in ("FW", "FW-NA"):
            if ref not in ms:
                continue
            L += ["", "Paired difference to %s (95%% CI; Holm-adjusted p over the four cells) and TOST (+-0.02) verdict at the Holm level:" % ref, "",
                  "| method | " + " | ".join("N=%d,%d src" % c for c in cells) + " |", "|" + "---|" * (len(cells) + 1)]
            for m in ms:
                if m == ref:
                    continue
                d = [[float(S(s, c, m, sd)["pdr"]) - float(S(s, c, ref, sd)["pdr"]) for sd in seeds] for c in cells]
                adj = statlib.holm([statlib.paired_t_p(x) for x in d])
                tadj = statlib.holm([statlib.tost_p(x, 0.02) for x in d])
                L.append("| %s | %s |" % (m, " | ".join("%+.3f +- %.3f (p=%.3f)%s" % (*statlib.mean_ci(x), p, " EQ" if t < 0.05 else "") for x, p, t in zip(d, adj, tadj))))
        # cost table (mean over cells and seeds)
        L += ["", "Cost and overhead (mean over the %d runs of the four cells):" % (len(seeds) * len(cells)), "",
              "| method | PDR | delay mean (s) | delay p95 (s) | J per delivered packet | ACK frames/pkt | control bytes/pkt | of which beacon | beacon share |", "|" + "---|" * 9]
        for m in ms:
            rows = [S(s, c, m, sd) for c in cells for sd in seeds]
            g = lambda k: mean(float(r[k]) for r in rows)
            L.append("| %s | %.3f | %.1f | %.1f | %.1f | %.2f | %.0f | %.0f | %.2f |" % (m, g("pdr"), g("delay_mean_s"), g("delay_p95_s"), g("energy_per_delivered_j"), g("ack_frames_per_delivered"),
                     g("ctrl_bytes_total_per_delivered"), g("ctrl_bytes_beacon_per_delivered"), g("ctrl_bytes_beacon_per_delivered") / max(1e-9, g("ctrl_bytes_total_per_delivered"))))
        # windows
        L += ["", "Windowed PDR (pooled over seeds; before = 500-800 s, during = 800-1400 s, after = 1500-2000 s) and recovery (two consecutive 100 s windows within 0.02 of the 500-800 s level):", "",
              "| cell | method | before | during | after | min 100 s window in 800-1400 s | recovery after burst end (s) | recovery from the dip inside the burst (s) |", "|" + "---|" * 8]
        for c in cells:
            for m in ms:
                pk = []
                for sd in seeds:
                    rows = list(csv.DictReader(open(os.path.join(res[(s, c, m, sd)], "packets.csv"))))
                    pk.append([(float(r["created_s"]), r["status"] == "delivered") for r in rows])
                ser = series(pk, 300.0, 2000.0)
                base = period(pk, 500.0, 800.0)
                dur = [(t, p) for t, p in ser if 800.0 <= t and t + 100.0 <= 1400.0]
                tmin, pmin = min(dur, key=lambda x: x[1])
                rec_end = recovery(ser, 1400.0, base)
                rec_dip = recovery(ser, tmin, base)
                L.append("| N=%d,%d | %s | %.3f | %.3f | %.3f | %.3f | %s | %s |" % (c[0], c[1], m, base, period(pk, 800.0, 1400.0), period(pk, 1500.0, 2000.0), pmin,
                         "n/r" if rec_end is None else "%.0f" % rec_end, "n/r" if rec_dip is None else "%.0f" % rec_dip))
        # regret in the burst window
        L += ["", "Regret per decision (utility = success x progress, ground truth) in 100 s bins inside 800-1400 s, pooled over seeds and cells:", "",
              "| method | regret vs per-hop oracle | regret vs FW | regret vs oracle, 500-800 s | decisions |", "|---|---|---|---|---|"]
        for m in ms:
            acc = collections.defaultdict(lambda: [0.0, 0.0, 0])
            for c in cells:
                for sd in seeds:
                    for r in csv.DictReader(open(os.path.join(res[(s, c, m, sd)], "regret_by_time.csv"))):
                        b = int(r["bin_start_s"])
                        k = "burst" if 800 <= b < 1400 else ("pre" if 500 <= b < 800 else None)
                        if k:
                            d = int(r["decisions"])
                            acc[k][0] += float(r["regret_vs_oracle"]) * d
                            acc[k][1] += float(r["regret_vs_fw"]) * d
                            acc[k][2] += d
            bur, pre = acc["burst"], acc["pre"]
            L.append("| %s | %.3f | %+.3f | %.3f | %d |" % (m, bur[0] / max(1, bur[2]), bur[1] / max(1, bur[2]), pre[0] / max(1, pre[2]), bur[2]))
        # theta trace
        cbm = [m for m in ms if m.startswith("CB") and m != "CB-warm-old"]
        L += ["", "Learned weights (mean over the heads that have labels, then over seeds and the two N=300 cells): x8 margin, x10 noise-corrected margin, x11 loss rate:", "",
              "| method | weight | t=300 | 500 | 800 | 1000 | 1200 | 1400 | 1700 | 2000 |", "|" + "---|" * 10]
        for m in cbm + (["CB-warm-old"] if "CB-warm-old" in ms else []):
            for idx, name in ((7, "x8"), (9, "x10"), (10, "x11")):
                if m == "CB-warm-old" and idx > 7:
                    continue
                row = []
                for t in (300, 500, 800, 1000, 1200, 1400, 1700, 2000):
                    vals = []
                    for c in cells[2:]:
                        for sd in seeds:
                            for r in csv.DictReader(open(os.path.join(res[(s, c, m, sd)], "theta_trace.csv"))):
                                if abs(float(r["t_s"]) - t) < 1 and float(r["heads_with_labels"]) > 0:
                                    vals.append(float(r["theta%d" % (idx + 1)]))
                    row.append("%.2f" % mean(vals) if vals else "-")
                L.append("| %s | %s | %s |" % (m, name, " | ".join(row)))
        L.append("")
    text = "\n".join(L) + "\n"
    open(os.path.join(a.outroot, "compare_%s.md" % "_".join(scns)), "w").write(text)
    print(text)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("part", choices=["headroom", "fit", "compare"])
    ap.add_argument("binary")
    ap.add_argument("outroot")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--nseeds", type=int, default=20)
    ap.add_argument("--scenarios", default="base,burst,shipping")
    ap.add_argument("--methods", default="FW,FW-NA,CB-cold,CB-warm,CB-warm-old,RR+FB,CB-warm-NA")
    x = ap.parse_args()
    os.makedirs(x.outroot, exist_ok=True)
    {"headroom": headroom, "fit": fit, "compare": compare}[x.part](x)
