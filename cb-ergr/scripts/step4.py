#!/usr/bin/env python3
"""Step 4 of the 7 Nov 2026 request: cost, memory, communication, learning behaviour and robustness.

Usage:
  step4.py cpu      BINARY OUTROOT          # CPU time per decision / label update, one process at a time (quiet machine)
  step4.py learning BINARY OUTROOT          # CB behaviour over time, 10 seeds, N = 300, 10 and 30 sources
  step4.py robust   BINARY OUTROOT          # CB under extreme load and N = 150
  step4.py comms    STEP3DIR                # control bytes per delivered packet and airtime share by class, from the Step 3 runs
  step4.py memory   STEP3DIR                # neighbour table, pending records, process memory, from the Step 3 runs
Each prints markdown and writes OUTROOT/<part>.md.
"""
import os as _os_nr; _os_nr.environ.setdefault("CB_ERGR_NUMBERED_RUNS", "0")  # sweeps need fixed result paths (run_sweep, tune, ... read OutDir/summary.csv)
import argparse, collections, csv, glob, math, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import perf, statlib


def kv(path):
    out = {}
    if os.path.exists(path):
        for r in csv.DictReader(open(path)):
            try:
                out[r["key"]] = float(r["value"])
            except ValueError:
                pass
    return out


def summary(out):
    return next(csv.DictReader(open(os.path.join(out, "summary.csv"))))


def run(binary, out, method, n, ns, seed, extra=(), **flags):
    sel, attrs = perf.METHODS[method]
    if not os.path.exists(os.path.join(out, "summary.csv")):
        cmd = [binary, f"--ConfigFile={perf.CFG}", f"--N={n}", f"--NumSources={ns}", f"--Seed={seed}", f"--Selector={sel}", f"--OutDir={out}",
               "--LogLinks=false", "--LogEvents=false"]
        for k, v in dict(dict(LogNodes="false", LogPackets="false", LogDecisions="false"), **flags).items():
            cmd.append(f"--{k}={v}")
        if attrs:
            cmd.append(f"--SelectorAttributes={attrs}")
        cmd += list(extra)
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(f"{out}: {r.stderr[-300:]}")
    return summary(out)


def mean(xs):
    xs = list(xs)
    return sum(xs) / len(xs) if xs else float("nan")


# ----------------------------------------------------------------------------------------------------------------------- cpu
def cpu(a):
    methods = ["RR", "RR+FB", "FW", "QL", "CARP-EWMA", "CB-cold", "CB-warm", "CB-warm+TD"]
    cells = [(300, 10), (300, 30)]
    seeds = [101, 102]
    rows = collections.defaultdict(list)
    for m in methods:
        for (n, ns) in cells:
            for s in seeds:       # one process at a time: the timings must not compete for the cores
                out = os.path.join(a.outroot, "cpu", f"N{n}_S{ns}", m, f"seed{s}")
                run(a.binary, out, m, n, ns, s, MeasureCpu="true")
                rows[m].append((kv(os.path.join(out, "cost.csv")), kv(os.path.join(out, "resources.csv"))))
    L = ["# Step 4a: CPU time in the simulator (this machine, -O3 build, double precision; mean over %d runs at N = 300 with 10 and 30 sources)" % (len(seeds) * len(cells)), "",
         "| method | Select: mean us | p95 | max | OnSent: mean us | p95 | OnLabel: mean us | p95 | max | wall time per run (s) | peak RSS (MB) |", "|" + "---|" * 11]
    for m in methods:
        cs = [c for c, r in rows[m]]
        rs = [r for c, r in rows[m]]
        g = lambda k: mean(c.get(k, float("nan")) for c in cs)
        mx = lambda k: max(c.get(k, 0.0) for c in cs)
        L.append("| %s | %.2f | %.2f | %.0f | %.2f | %.2f | %.2f | %.2f | %.0f | %.1f | %.1f |" % (m, g("cpu_select_us_mean"), g("cpu_select_us_p95"), mx("cpu_select_us_max"),
                 g("cpu_sent_us_mean"), g("cpu_sent_us_p95"), g("cpu_label_us_mean"), g("cpu_label_us_p95"), mx("cpu_label_us_max"),
                 mean(r["wall_s"] for r in rs), mean(r["max_rss_kb"] for r in rs) / 1024.0))
    L += ["", "(A call that takes 0 is not measured: a method without a learner has no OnLabel work. `max` is the largest single call and includes cold caches. "
          "wall time is for the whole 2000 s simulation of 300 nodes, all nodes, including the channel.)"]
    text = "\n".join(L) + "\n"
    open(os.path.join(a.outroot, "cpu.md"), "w").write(text)
    print(text)


# ----------------------------------------------------------------------------------------------------------------------- learning
def read_decisions(path):
    rows = []
    with open(path) as fh:
        for r in csv.DictReader(fh):
            rows.append(r)
    return rows


def learning(a):
    methods = ["FW", "CB-cold", "CB-warm", "CB-warm+Logit", "CB-warm+ProbStd", "CB-warm+TD"]
    cells = [(300, 10), (300, 30)]
    seeds = list(range(101, 111))
    jobs = [(m, n, ns, s) for (n, ns) in cells for m in methods for s in seeds]

    def work(j):
        m, n, ns, s = j
        out = os.path.join(a.outroot, "learn", f"N{n}_S{ns}", m, f"seed{s}")
        row = run(a.binary, out, m, n, ns, s, LogDecisions="true", LogPackets="true", LogNodes="true", ComputeRegret="true")
        return j, out

    with ThreadPoolExecutor(a.jobs) as pool:
        outs = dict(pool.map(work, jobs))
    L = ["# Step 4d: CB learning behaviour (N = 300, 10 seeds 101-110, Csma, calibrated channel)", ""]
    # per-gate / per-method table
    L += ["## Labels, ACK requests, PDR", "", "| N | src | method | PDR | labels per head | censored share of attempts | false-negative rate of the 0 labels | ACK-request rate | ACK frames/pkt | decisions with >= 2 viable arms | gap of the two best arms (mean) |",
          "|" + "---|" * 11]
    for (n, ns) in cells:
        for m in methods:
            pdr, lph, cens, fn, ar, ack, via, gap = [], [], [], [], [], [], [], []
            for s in seeds:
                out = outs[(m, n, ns, s)]
                sm = summary(out)
                pdr.append(float(sm["pdr"]))
                nodes = list(csv.DictReader(open(os.path.join(out, "nodes.csv"))))
                heads = [int(r["labels_1"]) + int(r["labels_0"]) for r in nodes if int(r["labels_1"]) + int(r["labels_0"]) > 0]
                lph.append(mean(heads))
                att = float(sm["attempts_total"])
                cens.append(float(sm["censored_labels"]) / att if att else 0.0)
                fn.append(float(sm["false_negative_rate"]))
                ar.append(float(sm["ar_rate"]))
                ack.append(float(sm["ack_frames_per_delivered"]))
                rb = list(csv.DictReader(open(os.path.join(out, "regret_by_time.csv"))))
                tot = sum(int(r["decisions"]) for r in rb)
                via.append(sum(float(r["share_two_viable"]) * int(r["decisions"]) for r in rb) / tot)
                gap.append(sum(float(r["gap_top2_mean"]) * int(r["decisions"]) for r in rb) / tot)
            L.append("| %d | %d | %s | %.3f | %.0f | %.3f | %.3f | %.2f | %.2f | %.2f | %.3f |" % (n, ns, m, mean(pdr), mean(lph), mean(cens), mean(fn), mean(ar), mean(ack), mean(via), mean(gap)))
    # regret over time and width over time
    for (n, ns) in cells:
        L += ["", "## Regret over time, N = %d, %d sources (mean per decision of the 10 seeds; regret in utility units = success x progress)" % (n, ns), "",
              "| window start (s) | " + " | ".join("%s vs oracle" % m for m in ["FW", "CB-cold", "CB-warm"]) + " | " + " | ".join("%s vs FW" % m for m in ["CB-cold", "CB-warm"]) + " | mean posterior width CB-cold | CB-warm |",
              "|" + "---|" * 8]
        bins = list(range(300, 2000, 200))
        agg = collections.defaultdict(lambda: collections.defaultdict(lambda: [0.0, 0.0, 0]))   # method -> bin -> sums
        for m in ["FW", "CB-cold", "CB-warm"]:
            for s in seeds:
                out = outs[(m, n, ns, s)]
                for r in csv.DictReader(open(os.path.join(out, "regret_by_time.csv"))):
                    b = (int(r["bin_start_s"]) - 300) // 200 * 200 + 300
                    d = int(r["decisions"])
                    agg[m][b][0] += float(r["regret_vs_oracle"]) * d
                    agg[m][b][1] += float(r["regret_vs_fw"]) * d
                    agg[m][b][2] += d
        width = collections.defaultdict(lambda: collections.defaultdict(lambda: [0.0, 0]))
        for m in ["CB-cold", "CB-warm"]:
            for s in seeds:
                for r in read_decisions(os.path.join(outs[(m, n, ns, s)], "decisions.csv")):
                    if r["learn"] != "1":
                        continue
                    b = int((float(r["time_s"]) - 300.0) // 200) * 200 + 300
                    b = max(300, b)
                    width[m][b][0] += float(r["width"])
                    width[m][b][1] += 1
        for b in bins:
            cells_ = []
            for m in ["FW", "CB-cold", "CB-warm"]:
                x = agg[m][b]
                cells_.append("%.3f" % (x[0] / x[2]) if x[2] else "-")
            for m in ["CB-cold", "CB-warm"]:
                x = agg[m][b]
                cells_.append("%+.3f" % (x[1] / x[2]) if x[2] else "-")
            for m in ["CB-cold", "CB-warm"]:
                w = width[m][b]
                cells_.append("%.3f" % (w[0] / w[1]) if w[1] else "-")
            L.append("| %d | %s |" % (b, " | ".join(cells_)))
        # windowed PDR: time until CB is within 0.02 of FW
        L += ["", "Windowed PDR (100 s windows, pooled over the 10 seeds): first window start from which two consecutive windows of CB are within 0.02 of FW's:", ""]
        def pooled(m):
            pk = [perf.windowed(os.path.join(outs[(m, n, ns, s)], "packets.csv")) for s in seeds]
            return perf.series(pk, 300.0, 2000.0)
        fw = pooled("FW")
        for m in ["CB-cold", "CB-warm"]:
            cb = pooled(m)
            t_ok = None
            for i in range(len(cb) - 1):
                if all((not math.isnan(cb[j][1])) and cb[j][1] >= fw[j][1] - 0.02 for j in (i, i + 1)):
                    t_ok = cb[i][0]
                    break
            L.append("- %s: %s; mean windowed difference to FW over the run %+.3f" % (m, ("%.0f s (= %.0f s after the data start)" % (t_ok, t_ok - 300.0)) if t_ok is not None else "never within the run",
                     mean(c[1] - f[1] for c, f in zip(cb, fw) if not math.isnan(c[1]) and not math.isnan(f[1]))))
    text = "\n".join(L) + "\n"
    open(os.path.join(a.outroot, "learning.md"), "w").write(text)
    print(text)


# ----------------------------------------------------------------------------------------------------------------------- robust
def robust(a):
    cases = [("N300, 45 sources", 300, 45), ("N150, 10 sources", 150, 10), ("N150, 45 sources", 150, 45)]
    seeds = [101, 102, 103]
    methods = ["CB-cold", "CB-warm", "CB-warm+TD"]
    L = ["# Step 4e: robustness (3 seeds each)", "",
         "| case | method | runs finished | non-finite decisions | non-finite theta nodes | Cholesky jitters | Cholesky resets (covariance not positive definite) | max abs theta | max trace(A) | PDR | peak pending attempts |", "|" + "---|" * 11]
    for name, n, ns in cases:
        for m in methods:
            done = 0
            agg = collections.defaultdict(float)
            pdr = []
            for s in seeds:
                out = os.path.join(a.outroot, "robust", name.replace(", ", "_").replace(" ", ""), m, f"seed{s}")
                try:
                    sm = run(a.binary, out, m, n, ns, s)
                except RuntimeError as e:
                    L.append("| %s | %s | CRASH seed %d: %s |" % (name, m, s, str(e)[:80]))
                    continue
                done += 1
                c = kv(os.path.join(out, "cost.csv"))
                for k in ["cb_nonfinite_decisions", "cb_nonfinite_theta_nodes", "cb_cholesky_jitters", "cb_cholesky_resets", "peak_pending_attempts"]:
                    agg[k] += c.get(k, 0.0)
                for k in ["max_abs_theta", "max_trace_A"]:
                    agg[k] = max(agg[k], c.get(k, 0.0))
                pdr.append(float(sm["pdr"]))
            L.append("| %s | %s | %d/%d | %d | %d | %d | %d | %.2f | %.1f | %.3f | %d |" % (name, m, done, len(seeds), agg["cb_nonfinite_decisions"], agg["cb_nonfinite_theta_nodes"],
                     agg["cb_cholesky_jitters"], agg["cb_cholesky_resets"], agg["max_abs_theta"], agg["max_trace_A"], mean(pdr), agg["peak_pending_attempts"] / max(1, done)))
    text = "\n".join(L) + "\n"
    open(os.path.join(a.outroot, "robust.md"), "w").write(text)
    print(text)


# ----------------------------------------------------------------------------------------------------------------------- comms / memory
def comms(a):
    methods = ["RR", "RR+FB", "FW", "CB-cold", "CB-warm", "QL", "CARP-EWMA", "Oracle"]
    L = ["# Step 4c: communication overhead (Step 3 runs, mean over 20 seeds and the four cells; bytes per delivered packet; airtime share of all transmitted frames)", "",
         "| method | PDR | DATA bytes/pkt | beacon: fixed part | beacon: receipt bitmaps | beacon: model blob | beacon: Q value | ACK | election | handover | NACK | control total | airtime: DATA | beacon | ACK | election+handover+NACK |",
         "|" + "---|" * 16]
    for m in methods:
        acc = collections.defaultdict(list)
        for d in glob.glob(os.path.join(a.dir, "N*_S*", m, "seed*")):
            sm = summary(d)
            c = kv(os.path.join(d, "cost.csv"))
            lb = {}
            for r in csv.DictReader(open(os.path.join(d, "loss_budget.csv"))):
                if r["group"] == "airtime_share":
                    lb[r["key"]] = float(r["value"])
            delivered = max(1.0, float(sm["packets_delivered"]))
            beacon_total = float(sm["ctrl_bytes_beacon_per_delivered"])
            parts = [c.get("beacon_base_bytes_per_delivered", 0.0), c.get("beacon_receipt_bytes_per_delivered", 0.0), c.get("beacon_blob_bytes_per_delivered", 0.0), c.get("beacon_q_bytes_per_delivered", 0.0)]
            # the parts are counted from the beacon headers (without the Aqua-Sim/MAC wrappers); scale them to the transmitted beacon bytes
            s = beacon_total / sum(parts) if sum(parts) > 0 else 0.0
            for k, v in zip(["b_base", "b_rcpt", "b_blob", "b_q"], parts):
                acc[k].append(v * s)
            acc["pdr"].append(float(sm["pdr"]))
            acc["data_tx"].append(float(sm["data_tx_per_delivered"]) * 167.0)    # data frame = 150 B payload + 17 B header
            for k, key in [("ack", "ctrl_bytes_ack_per_delivered"), ("elec", "ctrl_bytes_election_per_delivered"), ("hand", "ctrl_bytes_handover_per_delivered"),
                           ("nack", "ctrl_bytes_nack_per_delivered"), ("tot", "ctrl_bytes_total_per_delivered")]:
                acc[k].append(float(sm[key]))
            acc["a_data"].append(lb.get("data", 0.0))
            acc["a_bcn"].append(lb.get("beacon", 0.0))
            acc["a_ack"].append(lb.get("ack", 0.0))
            acc["a_oth"].append(lb.get("election", 0.0) + lb.get("handover", 0.0) + lb.get("nack", 0.0))
        g = lambda k: mean(acc[k])
        L.append("| %s | %.3f | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f | %.3f | %.3f | %.3f | %.3f |" % (m, g("pdr"), g("data_tx"), g("b_base"), g("b_rcpt"), g("b_blob"),
                 g("b_q"), g("ack"), g("elec"), g("hand"), g("nack"), g("tot"), g("a_data"), g("a_bcn"), g("a_ack"), g("a_oth")))
    text = "\n".join(L) + "\n"
    open(os.path.join(a.dir, "comms.md"), "w").write(text)
    print(text)


def memory(a):
    methods = ["RR+FB", "FW", "CB-cold", "CB-warm", "QL", "CARP-EWMA"]
    L = ["# Step 4b: memory in the simulator (Step 3 runs, mean over 20 seeds)", "",
         "| method | cell | neighbour rows per node: mean / p95 / max | peak pending-label records at one node | process peak RSS (MB) | wall time (s, 4 runs in parallel) |", "|---|---|---|---|---|---|"]
    for m in methods:
        for cell in ["N100_S10", "N100_S30", "N300_S10", "N300_S30"]:
            cs, rs = [], []
            for d in glob.glob(os.path.join(a.dir, cell, m, "seed*")):
                cs.append(kv(os.path.join(d, "cost.csv")))
                rs.append(kv(os.path.join(d, "resources.csv")))
            if not cs:
                continue
            L.append("| %s | %s | %.1f / %.0f / %.0f | %.0f (max %.0f) | %.1f | %.1f |" % (m, cell, mean(c["neighbour_rows_mean"] for c in cs), mean(c["neighbour_rows_p95"] for c in cs), max(c["neighbour_rows_max"] for c in cs),
                     mean(c["peak_pending_attempts"] for c in cs), max(c["peak_pending_attempts"] for c in cs), mean(r["max_rss_kb"] for r in rs) / 1024.0, mean(r["wall_s"] for r in rs)))
    c0 = kv(glob.glob(os.path.join(a.dir, "N300_S10", "CB-cold", "seed*", "cost.csv"))[0])
    L += ["", "Record sizes in the simulator (double precision, with padding): neighbour entry %d B, pending attempt %d B, held packet %d B, CB model object %d B, decision %d B." %
          (c0["sizeof_neighbour_entry_bytes"], c0["sizeof_pending_attempt_bytes"], c0["sizeof_held_packet_bytes"], c0["sizeof_cb_model_bytes"], c0["sizeof_decision_bytes"]),
          "On the wire a beacon row is the beacon header (fixed part, see `ErgrBeaconHeader::BaseBytes`); the model blob for sharing is 9 x int16 theta + 9 x uint16 diag(A) = 36 B; a planned HANDOVER carries theta and the full precision (see `ErgrHandoverHeader`)."]
    text = "\n".join(L) + "\n"
    open(os.path.join(a.dir, "memory.md"), "w").write(text)
    print(text)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("part", choices=["cpu", "learning", "robust", "comms", "memory"])
    ap.add_argument("a1")
    ap.add_argument("a2", nargs="?")
    ap.add_argument("--jobs", type=int, default=4)
    x = ap.parse_args()
    if x.part in ("comms", "memory"):
        x.dir = x.a1
        {"comms": comms, "memory": memory}[x.part](x)
    else:
        x.binary, x.outroot = x.a1, x.a2
        os.makedirs(x.outroot, exist_ok=True)
        {"cpu": cpu, "learning": learning, "robust": robust}[x.part](x)
