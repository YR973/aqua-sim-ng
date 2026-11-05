#!/usr/bin/env python3
"""Step 3 of the 7 Nov 2026 request: performance of FW, CB (cold, warm), QL, QlLegacy, CARP-EWMA, RR, RR+FB and the per-hop oracle.

Usage:
  perf.py main     BINARY OUTROOT [--seeds 101-120] [--jobs 4] [--methods FW,CB-cold,...]
  perf.py variants BINARY OUTROOT [--seeds 101-110] [--jobs 4]

main:     20 seeds x {N 100, 300} x {10, 30 sources}, Csma, configs/g-base.txt, paired (the same seed gives the same topology, traffic
          times and channel for every method). Tables: PDR per cell with the paired difference to FW (95% CI) and Holm-adjusted p values
          over the four cells; per-method cost metrics (delay, energy per delivered packet split into Tx and Rx, DATA tx and ACK frames
          per delivered packet, first node death, Jain index of residual energy); TOST equivalence of CB-cold / CB-warm to FW with a
          margin of +-0.02 PDR (equivalent if both one-sided tests reject at 5% after Holm over the cells).
variants: FW and CB-warm on N = 300 with 10 and 30 sources, 10 seeds, under (a) TPre = 0.1, (b) a permanent +10 dB noise band over the
          second cell column, (c) the same band as a burst from 800 s to 1400 s, (d) the heads of 6 cells killed at 1000 s;
          windowed PDR (100 s windows) and recovery times.
"""
import argparse, collections, csv, json, math, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import statlib

CFG = os.path.join(HERE, "..", "configs", "g-base.txt")
PARAMS = json.load(open(os.path.join(HERE, "params.json")))
PHYS = ",".join(str(v) for v in PARAMS["selector"]["kFwWeights"])

METHODS = collections.OrderedDict([
    ("RR", ("RoundRobin", "")),
    ("RR+FB", ("RoundRobin", "WithFeedback=true")),
    ("FW", ("FixedWeight", "")),
    ("CB-cold", ("CbV5", "AckMode=Every")),
    ("CB-warm", ("CbV5", "AckMode=Every;PriorWeights=" + PHYS + ";NeighbourPrior=true")),
    ("QL", ("QLearning", "")),
    ("QlLegacy", ("QLearning", "Legacy=true")),
    ("CARP-EWMA", ("CarpEwma", "")),
    ("Oracle", ("Oracle", "")),
    # CARP-EWMA with an ACK on every first attempt: the same feedback policy as FW / CB (its default is "no ACK, labels from bitmaps")
    ("CARP-ACK", ("CarpEwma", "AckRequest=true")),
    # ACK-gate variants (variants part only)
    ("CB-warm+Logit", ("CbV5", "AckMode=Gated;AckGate=Logit;PriorWeights=" + PHYS + ";NeighbourPrior=true")),
    ("CB-warm+ProbStd", ("CbV5", "AckMode=Gated;AckGate=ProbStd;PriorWeights=" + PHYS + ";NeighbourPrior=true")),
    ("CB-warm+TD", ("CbV5", "AckMode=Gated;AckGate=ThompsonDisagree;PriorWeights=" + PHYS + ";NeighbourPrior=true")),
])
MAIN = ["RR", "RR+FB", "FW", "CB-cold", "CB-warm", "QL", "QlLegacy", "CARP-EWMA", "Oracle"]
# CARP-ACK is run with `--methods FW,CARP-ACK` into the same OUTROOT afterwards; the tables then include it


def run(binary, out, method, n, ns, seed, extra=(), log_packets=False):
    if not os.path.exists(os.path.join(out, "summary.csv")):
        sel, attrs = METHODS[method]
        cmd = [binary, f"--ConfigFile={CFG}", f"--N={n}", f"--NumSources={ns}", f"--Seed={seed}", f"--Selector={sel}", f"--OutDir={out}",
               "--LogLinks=false", "--LogNodes=false", "--LogEvents=false", f"--LogPackets={'true' if log_packets else 'false'}"]
        cmd += ([f"--SelectorAttributes={attrs}"] if attrs else []) + list(extra)
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(f"{out}: {r.stderr[-300:]}")
    return next(csv.DictReader(open(os.path.join(out, "summary.csv"))))


def f(row, key):
    return float(row[key])


def metrics(row):
    delivered = max(1.0, f(row, "packets_delivered"))
    return {
        "pdr": f(row, "pdr"), "delay_mean": f(row, "delay_mean_s"), "delay_p95": f(row, "delay_p95_s"),
        "e_tx": f(row, "energy_tx_j") / delivered, "e_rx": f(row, "energy_rx_j") / delivered,
        "e_total": f(row, "energy_per_delivered_j"), "data_tx": f(row, "data_tx_per_delivered"),
        "ack": f(row, "ack_frames_per_delivered"), "fnd": f(row, "fnd_s"), "jain": f(row, "jain_residual_energy"),
        "hop": f(row, "hop_success_truth"),
    }


def main_part(args):
    lo, hi = (int(x) for x in args.seeds.split("-"))
    seeds = list(range(lo, hi + 1))
    cells = [(100, 10), (100, 30), (300, 10), (300, 30)]
    methods = args.methods.split(",") if args.methods else MAIN
    jobs = [(m, n, ns, s) for (n, ns) in cells for m in methods for s in seeds]
    # run the slow cells first so the pool stays busy
    jobs.sort(key=lambda j: -j[1])

    def work(j):
        m, n, ns, s = j
        return j, metrics(run(args.binary, os.path.join(args.outroot, f"N{n}_S{ns}", m, f"seed{s}"), m, n, ns, s))

    with ThreadPoolExecutor(args.jobs) as pool:
        res = dict(pool.map(work, jobs))
    os.makedirs(args.outroot, exist_ok=True)
    with open(os.path.join(args.outroot, "main.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        keys = ["pdr", "delay_mean", "delay_p95", "e_tx", "e_rx", "e_total", "data_tx", "ack", "fnd", "jain", "hop"]
        w.writerow(["method", "N", "sources", "seed"] + keys)
        for (m, n, ns, s), v in sorted(res.items()):
            w.writerow([m, n, ns, s] + [v[k] for k in keys])
    T = len(seeds)
    L = ["# Step 3: performance (Csma, calibrated channel, %d seeds %d-%d, paired)" % (T, lo, hi), ""]
    # --- table 1
    L += ["## PDR per cell (mean) and the paired difference to FW (95% CI); p = Holm-adjusted over the four cells", "",
          "| method | " + " | ".join("N=%d, %d src" % c for c in cells) + " | mean of cells |", "|" + "---|" * (len(cells) + 2)]
    for m in methods:
        cellsx = []
        for (n, ns) in cells:
            xs = [res[(m, n, ns, s)]["pdr"] for s in seeds]
            cellsx.append("%.3f" % (sum(xs) / T))
        L.append("| %s | %s | %.3f |" % (m, " | ".join(cellsx), sum(float(c) for c in cellsx) / len(cells)))
    L += ["", "| method | " + " | ".join("vs FW, N=%d, %d src" % c for c in cells) + " |", "|" + "---|" * (len(cells) + 1)]
    for m in methods:
        if m == "FW":
            continue
        diffs = [[res[(m, n, ns, s)]["pdr"] - res[("FW", n, ns, s)]["pdr"] for s in seeds] for (n, ns) in cells]
        adj = statlib.holm([statlib.paired_t_p(d) for d in diffs])
        L.append("| %s | %s |" % (m, " | ".join("%+.3f +- %.3f (p=%.3f)" % (*statlib.mean_ci(d), a) for d, a in zip(diffs, adj))))
    # --- table 2
    L += ["", "## Cost and fairness metrics (mean over the %d runs of the four cells; delay in s; energy in J per delivered packet)" % (T * len(cells)), "",
          "| method | PDR | delay mean | delay p95 | Tx J/pkt | Rx J/pkt | total J/pkt | DATA tx/pkt | ACK frames/pkt | first death (s) [runs with a death] | Jain |",
          "|" + "---|" * 11]
    for m in methods:
        rows = [res[(m, n, ns, s)] for (n, ns) in cells for s in seeds]
        avg = lambda k: sum(r[k] for r in rows) / len(rows)
        dead = [r["fnd"] for r in rows if r["fnd"] > 0]
        L.append("| %s | %.3f | %.1f | %.1f | %.1f | %.1f | %.1f | %.2f | %.2f | %s [%d/%d] | %.3f |" %
                 (m, avg("pdr"), avg("delay_mean"), avg("delay_p95"), avg("e_tx"), avg("e_rx"), avg("e_total"), avg("data_tx"), avg("ack"),
                  ("%.0f" % (sum(dead) / len(dead))) if dead else "-", len(dead), len(rows), avg("jain")))
    # --- table 3: TOST
    L += ["", "## Equivalence to FW (TOST, margin +-0.02 PDR; equivalent if the Holm-adjusted TOST p over the four cells is < 0.05)", "",
          "| method | cell | mean diff | 90% CI | TOST p | Holm p | verdict |", "|---|---|---|---|---|---|---|"]
    for m in methods:
        if not m.startswith("CB"):
            continue
        diffs = [[res[(m, n, ns, s)]["pdr"] - res[("FW", n, ns, s)]["pdr"] for s in seeds] for (n, ns) in cells]
        ps = [statlib.tost_p(d, 0.02) for d in diffs]
        adj = statlib.holm(ps)
        for (n, ns), d, p, a in zip(cells, diffs, ps, adj):
            mean, hw = statlib.mean_ci(d, 0.90)
            L.append("| %s | N=%d, %d src | %+.3f | [%+.3f, %+.3f] | %.3f | %.3f | %s |" %
                     (m, n, ns, mean, mean - hw, mean + hw, p, a, "equivalent" if a < 0.05 else "not shown equivalent"))
    text = "\n".join(L) + "\n"
    open(os.path.join(args.outroot, "main.md"), "w").write(text)
    print(text)


def windowed(packets_csv, width=100.0):
    rows = list(csv.DictReader(open(packets_csv)))
    return [(float(r["created_s"]), r["status"] == "delivered") for r in rows]


def series(all_packets, t0, t1, width=100.0, step=25.0):
    """Windowed PDR over the pooled packets of all seeds: list of (window start, pdr)."""
    out = []
    t = t0
    while t + width <= t1 + 1e-9:
        created = delivered = 0
        for packets in all_packets:
            for c, d in packets:
                if t <= c < t + width:
                    created += 1
                    delivered += 1 if d else 0
        out.append((t, delivered / created if created else float("nan")))
        t += step
    return out


def mean_between(ser, a, b):
    xs = [p for t, p in ser if a <= t and t + 100.0 <= b and not math.isnan(p)]
    return sum(xs) / len(xs) if xs else float("nan")


def recovery_after(ser, ref_time, baseline, tol=0.02, need=2):
    """First window start >= ref_time from which `need` consecutive windows are within tol of the baseline; returns seconds after ref_time."""
    for i, (t, p) in enumerate(ser):
        if t < ref_time:
            continue
        ok = all((not math.isnan(q)) and q >= baseline - tol for _, q in ser[i:i + need])
        if ok and len(ser[i:i + need]) == need:
            return t - ref_time
    return None


def variants_part(args):
    lo, hi = (int(x) for x in args.seeds.split("-"))
    seeds = list(range(lo, hi + 1))
    cells = [(300, 10), (300, 30)]
    scen = collections.OrderedDict([
        ("base", []),
        ("a: TPre 0.1 s", ["--TPre=0.1"]),
        ("b: +10 dB band (permanent)", ["--NoiseBandOffsetDb=10"]),
        ("c: +10 dB burst 800-1400 s", ["--NoiseBandOffsetDb=10", "--NoiseBandStartS=800", "--NoiseBandEndS=1400"]),
        ("d: 6 heads killed at 1000 s", ["--KillHeadsAtS=1000", "--KillHeadsCells=6"]),
    ])
    methods = ["FW", "CB-warm"]
    jobs = [(sc, m, n, ns, s) for sc in scen for (n, ns) in cells for m in methods for s in seeds]

    def work(j):
        sc, m, n, ns, s = j
        tag = sc.split(":")[0].replace(" ", "_")
        out = os.path.join(args.outroot, tag, f"N{n}_S{ns}", m, f"seed{s}")
        row = run(args.binary, out, m, n, ns, s, scen[sc], log_packets=True)
        return j, (metrics(row), windowed(os.path.join(out, "packets.csv")))

    with ThreadPoolExecutor(args.jobs) as pool:
        res = dict(pool.map(work, jobs))
    L = ["# Step 3, variants: FW against CB-warm (AckMode=Every), N = 300, %d seeds %d-%d" % (len(seeds), lo, hi), ""]
    L += ["| scenario | cell | method | PDR | PDR diff to FW (95% CI) | delay mean | Tx+Rx J/pkt | first death (s) [runs] |", "|" + "---|" * 8]
    for sc in scen:
        for (n, ns) in cells:
            for m in methods:
                rows = [res[(sc, m, n, ns, s)][0] for s in seeds]
                avg = lambda k: sum(r[k] for r in rows) / len(rows)
                dead = [r["fnd"] for r in rows if r["fnd"] > 0]
                diff = "-" if m == "FW" else "%+.3f +- %.3f" % statlib.mean_ci([res[(sc, m, n, ns, s)][0]["pdr"] - res[(sc, "FW", n, ns, s)][0]["pdr"] for s in seeds])
                L.append("| %s | N=%d, %d src | %s | %.3f | %s | %.1f | %.1f | %s [%d] |" % (sc, n, ns, m, avg("pdr"), diff, avg("delay_mean"), avg("e_total"),
                         ("%.0f" % (sum(dead) / len(dead))) if dead else "-", len(dead)))
    # recovery
    L += ["", "## Windowed PDR (100 s windows, pooled over seeds) and recovery times", "",
          "Baseline = mean windowed PDR in 500-800 s (burst) or 600-1000 s (failures). Recovery = seconds after the reference time until two consecutive windows are within 0.02 of the baseline (n/r = not reached by 2000 s).",
          "",
          "| scenario | cell | method | baseline PDR | PDR in event (first 100 s) | PDR in event (last 300 s) | PDR after event | recovery after event end/injection (s) |", "|" + "---|" * 8]
    for sc, ev0, ev1, bw in [("c: +10 dB burst 800-1400 s", 800.0, 1400.0, (500.0, 800.0)), ("d: 6 heads killed at 1000 s", 1000.0, 1000.0, (600.0, 1000.0))]:
        for (n, ns) in cells:
            for m in methods:
                pk = [res[(sc, m, n, ns, s)][1] for s in seeds]
                ser = series(pk, 300.0, 2000.0)
                base = mean_between(ser, bw[0], bw[1])
                first = mean_between(ser, ev0, ev0 + 100.0)
                last = mean_between(ser, ev1 - 300.0, ev1) if ev1 > ev0 else float("nan")
                after = mean_between(ser, ev1 + 100.0, 2000.0)
                rec = recovery_after(ser, ev1 if ev1 > ev0 else ev0 + 25.0, base)
                L.append("| %s | N=%d, %d src | %s | %.3f | %.3f | %s | %.3f | %s |" % (sc, n, ns, m, base, first, ("%.3f" % last) if not math.isnan(last) else "-", after,
                         ("%.0f" % rec) if rec is not None else "n/r"))
    text = "\n".join(L) + "\n"
    open(os.path.join(args.outroot, "variants.md"), "w").write(text)
    print(text)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("part", choices=["main", "variants"])
    ap.add_argument("binary")
    ap.add_argument("outroot")
    ap.add_argument("--seeds", default=None)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--methods", default="")
    a = ap.parse_args()
    a.seeds = a.seeds or ("101-120" if a.part == "main" else "101-110")
    (main_part if a.part == "main" else variants_part)(a)
