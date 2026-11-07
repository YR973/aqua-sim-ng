#!/usr/bin/env python3
"""k = 1 experiment (one head per cell), 6-7 Nov 2026.

  k1.py fit    BINARY OUTROOT    CB warm-start prior per (test, k) from RR+FB decision logs, seeds 121-130 (NOT 101-120)
  k1.py run    BINARY OUTROOT    T1-T4 x k in {1, 2} x {RR, RR+FB, FW, CB-warm, Oracle} x {N300/10, N100/30} x seeds 101-120
  k1.py extra  BINARY OUTROOT    T1, k = 1, N300/10 at 0 m/s (RR, FW) for the comparison with the P0 reproduction (test 16)
  k1.py report BINARY OUTROOT    tables (results/k1/*.md, *.csv)

Tests: T1 ideal links + old cells (433 m, Paper32 neighbours), T2 ideal + new cells (750 m, Adjacent), T3 calibrated + old, T4 calibrated + new.
Every run: configs/g-base.txt (Csma, 17.8 kbit/s, TPre 0, 300 J, 3 sinks), ChannelAwareBeacons=true, LabelRule=Evidence (as round 5), base scenario.
Runs are resumable: a run whose summary.csv exists is not repeated.
"""
import argparse, collections, csv, json, math, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import statlib

GBASE = os.path.join(HERE, "..", "configs", "g-base.txt")
CELLS = [(300, 10), (100, 30)]
SEEDS = list(range(101, 121))
FIT_SEEDS = list(range(121, 131))
KS = [1, 2]
OLD = ["--CellSizeM=433.0127", "--NeighbourRule=Paper32"]
NEW = ["--CellSizeM=750", "--NeighbourRule=Adjacent"]
IDEAL = ["--IdealLinks=true", "--ShadowSigmaDb=0"]
CALIB = []
TESTS = collections.OrderedDict([("T1", IDEAL + OLD), ("T2", IDEAL + NEW), ("T3", CALIB + OLD), ("T4", CALIB + NEW)])
TEST_NAME = {"T1": "ideal links + old cells (433 m, 32 neighbours)", "T2": "ideal links + new cells (750 m, 26 adjacent)",
             "T3": "calibrated links + old cells", "T4": "calibrated links + new cells"}
METHODS = ["RR", "RR+FB", "FW", "CB-warm", "Oracle"]


def fit_file(outroot, t, k):
    return os.path.join(outroot, "fit", f"{t}_k{k}.json")


def method_spec(outroot, m, t, k):
    if m == "RR":
        return "RoundRobin", ""
    if m == "RR+FB":
        return "RoundRobin", "WithFeedback=true"
    if m == "FW":
        return "FixedWeight", ""
    if m == "Oracle":
        return "Oracle", ""
    if m == "CB-warm":
        w = json.load(open(fit_file(outroot, t, k)))["w10"]
        return "CbV5", "AckMode=Every;ChannelFeatures=true;NeighbourPrior=true;PriorWeights=" + ",".join("%.6g" % x for x in w)
    raise KeyError(m)


def run_one(binary, outroot, t, k, m, n, ns, seed, tag="", extra=(), decisions=False, cf=False, sel_override=None):
    out = os.path.join(outroot, t + tag, f"k{k}", f"N{n}_S{ns}", m, f"seed{seed}")
    if not os.path.exists(os.path.join(out, "summary.csv")):
        sel, attrs = sel_override or method_spec(outroot, m, t, k)
        cmd = [binary, f"--ConfigFile={GBASE}", f"--N={n}", f"--NumSources={ns}", f"--Seed={seed}", f"--Selector={sel}", f"--OutDir={out}",
               "--ChannelAwareBeacons=true", "--LabelRule=Evidence", f"--HeadsPerCell={k}", "--LogLinks=false", "--LogNodes=false", "--LogEvents=false",
               "--LogPackets=true", "--LogAttempts=true", "--ComputeRegret=true", f"--LogDecisions={'true' if decisions else 'false'}",
               f"--LogCounterfactual={'true' if cf else 'false'}"] + TESTS[t] + list(extra)
        if attrs:
            cmd.append(f"--SelectorAttributes={attrs}")
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(f"{out}: {r.stderr[-400:]}")
    return out


def pool(jobs, fn, n):
    with ThreadPoolExecutor(n) as p:
        return list(p.map(fn, jobs))


def fit(a):
    import numpy as np
    for t in TESTS:
        for k in KS:
            if os.path.exists(fit_file(a.outroot, t, k)):
                continue
            jobs = [(c, sd) for c in CELLS for sd in FIT_SEEDS]
            outs = pool(jobs, lambda j: run_one(a.binary, a.outroot, t, k, "RR+FB", j[0][0], j[0][1], j[1], tag="_fit", decisions=True,
                                                sel_override=("RoundRobin", "WithFeedback=true")), a.jobs)
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
            info = {"test": t, "k": k, "rows": int(len(y)), "positive_rate": float(y.mean()),
                    "logloss": float(-np.mean(y * np.log(p) + (1 - y) * np.log(1 - p))), "w10": [float(v) for v in w],
                    "fit_seeds": "121-130", "cells": "N300/10, N100/30"}
            os.makedirs(os.path.dirname(fit_file(a.outroot, t, k)), exist_ok=True)
            json.dump(info, open(fit_file(a.outroot, t, k), "w"), indent=1)
            print(t, k, info["rows"], round(info["positive_rate"], 3), [round(v, 2) for v in w], flush=True)
            # the decision logs are large: keep the weights, drop the logs
            for out in outs:
                subprocess.run(["rm", "-f", os.path.join(out, "decisions.csv")])


def main_runs(a):
    jobs = [(t, k, m, c, sd) for t in TESTS for k in KS for c in CELLS for m in METHODS for sd in SEEDS]
    jobs.sort(key=lambda j: (j[3][0] != 300, j[4]))   # N300 first, seeds interleaved
    done = [0]

    def work(j):
        t, k, m, c, sd = j
        run_one(a.binary, a.outroot, t, k, m, c[0], c[1], sd, cf=(m == "FW"))
        done[0] += 1
        if done[0] % 50 == 0:
            print("done", done[0], "of", len(jobs), flush=True)
    pool(jobs, work, a.jobs)


def extra_runs(a):
    jobs = [(m, sd) for m in ("RR", "FW") for sd in SEEDS]
    pool(jobs, lambda j: run_one(a.binary, a.outroot, "T1", 1, j[0], 300, 10, j[1], tag="_speed0", extra=("--CurrentSpeed=0",)), a.jobs)


# ------------------------------------------------------------------------------------------------------------------ reporting
def S(out):
    return next(csv.DictReader(open(os.path.join(out, "summary.csv"))))


def kv(path):
    d = collections.defaultdict(float)
    for r in csv.DictReader(open(path)):
        try:
            d[(r.get("group", ""), r["key"])] = float(r["value"])
        except ValueError:
            pass
    return d


def headsfile(path):
    d = {}
    for r in csv.DictReader(open(path)):
        try:
            d[r["key"]] = float(r["value"])
        except ValueError:
            pass
    return d


def path_of(a, t, k, c, m, sd, tag=""):
    return os.path.join(a.outroot, t + tag, f"k{k}", f"N{c[0]}_S{c[1]}", m, f"seed{sd}")


def mci(xs):
    return statlib.mean_ci(xs) if len(xs) > 1 else (xs[0], 0.0)


def fmt(m, h, d=3):
    return f"{m:.{d}f} ±{h:.{d}f}"


def packets_of(out):
    import pandas as pd
    return pd.read_csv(os.path.join(out, "packets.csv"))


def attempts_of(out):
    import pandas as pd
    return pd.read_csv(os.path.join(out, "attempts.csv"))


def per_run(a, t, k, c, m, sd):
    """All the numbers of one run that the tables use."""
    import numpy as np
    out = path_of(a, t, k, c, m, sd)
    s = S(out)
    f = lambda key: float(s[key])
    D = max(1.0, f("packets_delivered"))
    created = max(1.0, f("packets_created"))
    L = kv(os.path.join(out, "loss_budget.csv"))
    H = headsfile(os.path.join(out, "heads.csv"))
    r = {"pdr": f("pdr"), "delay": f("delay_mean_s"), "p95": f("delay_p95_s"), "e": f("energy_per_delivered_j"),
         "e_tx": f("energy_tx_j") / D, "e_rx": f("energy_rx_j") / D, "e_idle": f("energy_idle_j") / D, "fnd": f("fnd_s"),
         "regret": f("regret_mean"), "created": created, "hop_success": f("hop_success_truth"), "retx": f("retx_per_delivered"),
         "elections_h": f("elections_per_hour"), "cells_with_head": f("cells_with_head_share"), "dead_false": f("dead_false_declarations")}
    for key in ("no_eligible_forwarder", "max_tx_reached", "hop_budget_exhausted", "queue_full", "node_died_holding", "ttl_expired"):
        r["drop_" + key] = f("drop_" + key) / created
    r["in_flight"] = f("packets_in_flight") / created
    # airtime and energy by frame type
    types = ["data", "ack", "beacon", "election", "handover", "nack"]
    tot_air = sum(L[("airtime_share", ty)] * 1.0 for ty in types) or 1.0
    for ty in types:
        r["air_" + ty] = L[("airtime_share", ty)]
        r["fr_" + ty] = L[("frames", ty)] / D
        r["en_" + ty] = f(f"energy_{ty}_tx_per_delivered_j") + f(f"energy_{ty}_rx_per_delivered_j")
    en_tot = sum(r["en_" + ty] for ty in types) or 1.0
    for ty in types:
        r["ens_" + ty] = r["en_" + ty] / en_tot
    # hop failure causes (share of failed attempts)
    att = L[("hop", "attempts")]
    fails = att - L[("hop", "delivered")]
    r["attempts_per_created"] = att / created
    for key, name in (("per_loss", "per"), ("collision_with_data", "col_data"), ("collision_with_beacon_hello", "col_beacon"),
                      ("collision_with_ack_nack", "col_ack"), ("half_duplex", "half"), ("receiver_dead", "dead"),
                      ("out_of_range", "range"), ("unexplained_phy_busy", "unexpl"), ("never_on_air", "never")):
        r["why_" + name] = L[("hop", key)] / fails if fails else 0.0
    # heads.csv
    for key in ("candidates_mean", "share_two_or_more", "share_void", "void_cells", "decision_cells", "cells_empty_share", "occupied_with_head_share",
                "heads_per_occupied_cell", "occupied_with_0_heads", "occupied_with_1_head", "occupied_with_2_heads", "occupied_with_3plus_heads",
                "elections_after_warmup", "over_k_episodes", "over_k_max_s", "over_k_longer_than_slot", "over_k_cell_seconds"):
        r["h_" + key] = H.get(key, float("nan"))
    r["h_stepdown"] = sum(v for kk, v in H.items() if kk.startswith("tenure_end:outranked"))
    r["h_leftcell"] = H.get("tenure_end:left cell", 0.0)
    r["h_died"] = H.get("tenure_end:died", 0.0)
    # packets: where lost
    pk = packets_of(out)
    pk = pk[pk.created_s >= f("warmup_s")]
    n = float(len(pk))
    delivered = pk[pk.status == "delivered"]
    r["mean_hops"] = float(delivered.hops.mean()) if len(delivered) else float("nan")
    lost = pk[pk.status != "delivered"]
    plen = lost.path.astype(str).str.count("-") + 1
    src_member = lost.src_head == 0
    never = (lost.data_tx == 0)
    r["loss_total"] = len(lost) / n
    r["loss_source_nohead"] = float(((plen == 1) & never & (lost.drop_reason == "NO_ELIGIBLE_FORWARDER")).sum()) / n
    r["loss_source_other"] = float(((plen == 1) & never & (lost.drop_reason != "NO_ELIGIBLE_FORWARDER")).sum()) / n
    sent = ~never
    hand = sent & (plen == 1) & src_member
    r["loss_handoff"] = float(hand.sum()) / n
    # hop index counted over head hops: the hop out of the n-th node of the path (the hand-off, if any, is its own row)
    idx = plen - src_member.astype(int)
    for h in range(1, 6):
        mask = sent & ~hand & ((idx == h) if h < 5 else (idx >= 5))
        r[f"loss_hop{h}"] = float(mask.sum()) / n
    # loss at the last hop to a sink: the packet's last attempt went to a sink
    at = attempts_of(out)
    at = at.sort_values("t")
    last = at.groupby(["source", "seq"]).tail(1)
    lk = lost.merge(last[["source", "seq", "fwd_sink", "cause"]], on=["source", "seq"], how="left")
    r["loss_lasthop_sink"] = float((lk.fwd_sink == 1).sum()) / n
    r["loss_ttl"] = float((lost.status == "in_flight").sum()) / n
    # attempts by number and cause (share of failed attempts), space tables
    failed = at[at.received == 0]
    r["fail_first"] = float((failed.nth_here == 1).sum()) / max(1, len(failed))
    r["att_first"] = float((at.nth_here == 1).sum()) / max(1, len(at))
    for lab, sub in (("first", failed[failed.nth_here == 1]), ("retry", failed[failed.nth_here > 1])):
        for cause, name in (("per_loss", "per"), ("collision_with_data", "col_data"), ("collision_with_beacon_hello", "col_beacon"),
                            ("collision_with_ack_nack", "col_ack"), ("half_duplex", "half"), ("receiver_dead", "dead"),
                            ("out_of_range", "range"), ("unexplained_phy_busy", "unexpl"), ("never_on_air", "never")):
            r[f"{lab}_{name}"] = float((sub.cause == cause).sum()) / max(1, len(sub))
    r["fail_rate_first"] = float(((at.nth_here == 1) & (at.received == 0)).sum()) / max(1, (at.nth_here == 1).sum())
    r["fail_rate_retry"] = float(((at.nth_here > 1) & (at.received == 0)).sum()) / max(1, (at.nth_here > 1).sum())
    # where in space (sender position at the attempt)
    for i, (lo, hi) in enumerate([(0, 750), (750, 1500), (1500, 2250), (2250, 1e9)]):
        m_ = (at.depth_m >= lo) & (at.depth_m < hi)
        r[f"dep{i}_att"] = float(m_.sum()) / created
        r[f"dep{i}_fail"] = float((m_ & (at.received == 0)).sum()) / max(1.0, float(m_.sum()))
        ml = (lost.drop_depth_m >= lo) & (lost.drop_depth_m < hi)
        r[f"dep{i}_drop"] = float(ml.sum()) / n
    for i, (lo, hi) in enumerate([(0, 750), (750, 1500), (1500, 2250), (2250, 1e9)]):
        m_ = (at.dsink_m >= lo) & (at.dsink_m < hi)
        r[f"ds{i}_att"] = float(m_.sum()) / created
        r[f"ds{i}_fail"] = float((m_ & (at.received == 0)).sum()) / max(1.0, float(m_.sum()))
        ml = (lost.drop_dsink_m >= lo) & (lost.drop_dsink_m < hi)
        r[f"ds{i}_drop"] = float(ml.sum()) / n
    sink_att = at[at.fwd_sink == 1]
    r["sink_att_fail"] = float((sink_att.received == 0).sum()) / max(1, len(sink_att))
    r["sink_att_share"] = len(sink_att) / max(1, len(at))
    return r


def counterfactual_fixable(a, t, k, c, sd):
    """Share of failed attempts of FW where at least one other eligible candidate would have received the frame (ground truth, log only)."""
    import pandas as pd
    out = path_of(a, t, k, c, "FW", sd)
    cf = pd.read_csv(os.path.join(out, "counterfactual.csv"))
    cf = cf[(cf.chosen_received == 0) & (cf.n_cand >= 2)]
    if not len(cf):
        return 0.0, 0.0
    fix = cf.candidates.astype(str).apply(lambda s: any(p.endswith(":R") for p in s.split("|")))
    allf = float(len(pd.read_csv(os.path.join(out, "counterfactual.csv"))))
    return float(fix.sum()) / max(1.0, allf), float(fix.mean())


def cache_path(a):
    return os.path.join(a.outroot, "runs_cache.json")


def collect(a):
    cp = cache_path(a)
    cache = json.load(open(cp)) if os.path.exists(cp) else {}
    jobs = [(t, k, c, m, sd) for t in TESTS for k in KS for c in CELLS for m in METHODS for sd in SEEDS]
    todo = [j for j in jobs if "|".join(map(str, (j[0], j[1], j[2][0], j[2][1], j[3], j[4]))) not in cache and
            os.path.exists(os.path.join(path_of(a, j[0], j[1], j[2], j[3], j[4]), "summary.csv"))]

    def work(j):
        t, k, c, m, sd = j
        r = per_run(a, t, k, c, m, sd)
        if m == "FW":
            r["cf_fixable_all"], r["cf_fixable_of_fail"] = counterfactual_fixable(a, t, k, c, sd)
        return "|".join(map(str, (t, k, c[0], c[1], m, sd))), r
    for key, r in pool(todo, work, a.jobs):
        cache[key] = r
    json.dump(cache, open(cp, "w"))
    return cache


def get(cache, t, k, c, m, key):
    out = []
    for sd in SEEDS:
        r = cache.get("|".join(map(str, (t, k, c[0], c[1], m, sd))))
        if r is None or key not in r:
            return None
        out.append(r[key])
    return out


def paired(xs, ys):
    if xs is None or ys is None:
        return None
    d = [x - y for x, y in zip(xs, ys)]
    m, h = mci(d)
    return m, h, statlib.paired_t_p(d)


def report(a):
    cache = collect(a)
    P = lambda t, k, c, m, key: get(cache, t, k, c, m, key)
    mean_ = lambda xs: (sum(xs) / len(xs)) if xs else float("nan")
    cname = lambda c: f"N{c[0]}/{c[1]}"
    L = []
    w = L.append

    # ---------------------------------------------------------------- headline
    w("# k = 1 experiment: results (20 paired seeds 101-120, Csma, base scenario, 2000 s, warm-up 300 s)\n")
    w("Test definitions: " + "; ".join(f"**{t}** = {TEST_NAME[t]}" for t in TESTS) + ".\n")
    rows = []
    # (a) headline
    w("## 2a. Headline: PDR, delay, energy, first node death\n")
    w("PDR mean ±95% CI over 20 seeds. Δk = k1 - k2 is the paired difference (same seeds) with its 95% CI and Holm-adjusted p over the table. ΔFW = method - FW (same test, k, cell).\n")
    w("| test | cell | method | PDR k=1 | PDR k=2 | Δk (k1-k2) | p(Holm) | ΔFW k=1 | ΔFW k=2 | delay s k1 (mean/p95) | delay s k2 | J/pkt k1 (tx/rx/idle) | J/pkt k2 | first death s k1 | k2 |")
    w("|" + "---|" * 15)
    pl = []
    entries = []
    for t in TESTS:
        for c in CELLS:
            for m in METHODS:
                p1, p2 = P(t, 1, c, m, "pdr"), P(t, 2, c, m, "pdr")
                dk = paired(p1, p2)
                entries.append((t, c, m, dk))
                pl.append(dk[2] if dk else 1.0)
    adj = statlib.holm(pl)
    for (t, c, m, dk), ph in zip(entries, adj):
        g = lambda k, key: P(t, k, c, m, key)
        def one(k):
            x = g(k, "pdr")
            return fmt(*mci(x)) if x else "-"
        def dfw(k):
            d = paired(g(k, "pdr"), P(t, k, c, "FW", "pdr"))
            return "-" if (not d or m == "FW") else f"{d[0]:+.3f} ±{d[1]:.3f}"
        def dl(k):
            x, y = g(k, "delay"), g(k, "p95")
            return f"{mean_(x):.1f}/{mean_(y):.1f}" if x and y else "-"
        def en(k):
            x = g(k, "e")
            if not x:
                return "-"
            return f"{mean_(x):.1f} ({mean_(g(k,'e_tx')):.0f}/{mean_(g(k,'e_rx')):.0f}/{mean_(g(k,'e_idle')):.0f})"
        def fnd(k):
            x = g(k, "fnd")
            return f"{mean_([v for v in x if v >= 0]):.0f} ({sum(1 for v in x if v >= 0)}/20)" if x else "-"
        w(f"| {t} | {cname(c)} | {m} | {one(1)} | {one(2)} | " + (f"{dk[0]:+.3f} ±{dk[1]:.3f}" if dk else "-") + f" | {ph:.3f} | {dfw(1)} | {dfw(2)} | {dl(1)} | {dl(2)} | {en(1)} | {en(2)} | {fnd(1)} | {fnd(2)} |")
    w("\n(first death: mean over the runs in which a node died, with the number of such runs of 20.)\n")

    def table(title, cols, rowsel, note=""):
        w(f"## {title}\n")
        if note:
            w(note + "\n")
        w("| test | k | cell | method | " + " | ".join(c[0] for c in cols) + " |")
        w("|" + "---|" * (4 + len(cols)))
        for t in TESTS:
            for k in KS:
                for c in CELLS:
                    for m in rowsel:
                        vals = []
                        for _, key, kind in cols:
                            x = P(t, k, c, m, key)
                            if x is None:
                                vals.append("-")
                            elif kind == "pct":
                                vals.append(f"{100 * mean_(x):.1f}")
                            elif kind == "d2":
                                vals.append(f"{mean_(x):.2f}")
                            else:
                                vals.append(f"{mean_(x):.3f}")
                        w(f"| {t} | {k} | {cname(c)} | {m} | " + " | ".join(vals) + " |")
        w("")

    table("2b. Where packets are lost, as % of created packets",
          [("lost total", "loss_total", "pct"), ("source: no head row / solicit failed", "loss_source_nohead", "pct"),
           ("source: other", "loss_source_other", "pct"), ("member→head hand-off", "loss_handoff", "pct"), ("head hop 1", "loss_hop1", "pct"),
           ("hop 2", "loss_hop2", "pct"), ("hop 3", "loss_hop3", "pct"), ("hop 4", "loss_hop4", "pct"), ("hop ≥5", "loss_hop5", "pct"),
           ("(of lost) last attempt went to a sink", "loss_lasthop_sink", "pct"),
           ("drop MAX_TX", "drop_max_tx_reached", "pct"), ("drop NO_ELIGIBLE/void", "drop_no_eligible_forwarder", "pct"),
           ("drop HOP_BUDGET", "drop_hop_budget_exhausted", "pct"), ("drop TTL_EXPIRED", "drop_ttl_expired", "pct"),
           ("drop QUEUE_FULL", "drop_queue_full", "pct"), ("drop NODE_DIED", "drop_node_died_holding", "pct"), ("in flight at end", "in_flight", "pct")],
          METHODS,
          "Position: the hop out of the n-th head on the path where the packet's last attempt failed; 'hand-off' = a member source's hop to its head. "
          "Drop reasons are the routing layer's own reasons (RR without hop ACKs cannot see losses: its losses end as TTL_EXPIRED).")
    table("2c. Why hops fail, % of failed attempts",
          [("PER", "why_per", "pct"), ("DATA–DATA collision", "why_col_data", "pct"), ("beacon/Hello collision", "why_col_beacon", "pct"),
           ("ACK collision", "why_col_ack", "pct"), ("half-duplex", "why_half", "pct"), ("receiver dead", "why_dead", "pct"),
           ("out of range", "why_range", "pct"), ("unexplained", "why_unexpl", "pct"), ("hop success", "hop_success", "f3"),
           ("attempts / created pkt", "attempts_per_created", "d2")], METHODS)
    table("2c'. Failed attempts split by attempt number: share of first attempts among all attempts, failure rate first / retry, and causes of failed FIRST attempts | of RETRIES (%)",
          [("first attempts %", "att_first", "pct"), ("fail rate first", "fail_rate_first", "f3"), ("fail rate retry", "fail_rate_retry", "f3"),
           ("first: PER", "first_per", "pct"), ("first: DATA col", "first_col_data", "pct"), ("first: beacon col", "first_col_beacon", "pct"),
           ("first: ACK col", "first_col_ack", "pct"), ("first: half-duplex", "first_half", "pct"), ("first: dead", "first_dead", "pct"),
           ("first: range", "first_range", "pct"), ("retry: PER", "retry_per", "pct"), ("retry: DATA col", "retry_col_data", "pct"),
           ("retry: beacon col", "retry_col_beacon", "pct"), ("retry: ACK col", "retry_col_ack", "pct"), ("retry: half-duplex", "retry_half", "pct"),
           ("retry: dead", "retry_dead", "pct"), ("retry: range", "retry_range", "pct")], ["RR+FB", "FW", "CB-warm", "Oracle"],
          "RR (no hop ACKs) has no retries by design; its attempts are all first attempts.")
    sp = []
    for pre, nm in (("dep", "depth layer (sender depth, m)"), ("ds", "distance of the sender to the nearest sink (m)")):
        for i, lab in enumerate(["0-750", "750-1500", "1500-2250", ">2250"]):
            sp += [(f"{lab}: attempts / created", f"{pre}{i}_att", "d2"), (f"{lab}: attempt fail rate", f"{pre}{i}_fail", "f3"),
                   (f"{lab}: packets lost there %", f"{pre}{i}_drop", "pct")]
        table(f"2d. Where in space: by {nm}", [x for x in sp], ["RR+FB", "FW", "CB-warm"],
              "Attempts per created packet, failure rate of those attempts, and the % of created packets whose drop happened there (drops are observed at the node that dropped; "
              "TTL losses of RR are not located).")
        sp = []
    table("2d'. The last hop: attempts toward a sink",
          [("share of all attempts", "sink_att_share", "pct"), ("failure rate of attempts to a sink", "sink_att_fail", "f3")], ["RR+FB", "FW", "CB-warm", "Oracle"])
    table("2e. Airtime share % (by frame type), frames per delivered packet, energy share % (Tx+Rx)",
          [(f"air {ty}", "air_" + ty, "pct") for ty in ("data", "ack", "beacon", "election", "handover", "nack")] +
          [(f"frames/pkt {ty}", "fr_" + ty, "d2") for ty in ("data", "ack", "beacon", "election", "handover", "nack")] +
          [(f"energy {ty}", "ens_" + ty, "pct") for ty in ("data", "ack", "beacon", "election", "handover", "nack")], METHODS)
    table("2f. Topology",
          [("mean hops of delivered", "mean_hops", "d2"), ("candidates / decision", "h_candidates_mean", "d2"), ("decisions with ≥2 eligible heads %", "h_share_two_or_more", "pct"),
           ("void decisions %", "h_share_void", "pct"), ("void cells (distinct)", "h_void_cells", "d2"), ("empty cells %", "h_cells_empty_share", "pct"),
           ("occupied cells with a head %", "h_occupied_with_head_share", "pct")], METHODS)
    # Step 0 election table
    w("## Step 0. Heads per cell and elections, k = 1 vs k = 2 (RR+FB, mean of the two cells' seeds)\n")
    w("| test | k | cell | occupied cells with 0 / 1 / 2 / 3+ heads (%) | heads per occupied cell | elections after warm-up | tenure ends: step-down / left cell / died | over-k episodes (count, longest s, longer than 1 slot) | false dead declarations | cells with a head % |")
    w("|" + "---|" * 10)
    for t in TESTS:
        for k in KS:
            for c in CELLS:
                g = lambda key: P(t, k, c, "RR+FB", key)
                if g("h_elections_after_warmup") is None:
                    continue
                mm = lambda key: mean_(g(key))
                w(f"| {t} | {k} | {cname(c)} | {100*mm('h_occupied_with_0_heads'):.1f} / {100*mm('h_occupied_with_1_head'):.1f} / {100*mm('h_occupied_with_2_heads'):.1f} / {100*mm('h_occupied_with_3plus_heads'):.1f} | "
                  f"{mm('h_heads_per_occupied_cell'):.2f} | {mm('h_elections_after_warmup'):.0f} | {mm('h_stepdown'):.0f} / {mm('h_leftcell'):.0f} / {mm('h_died'):.1f} | "
                  f"{mm('h_over_k_episodes'):.0f}, {max(g('h_over_k_max_s')):.0f}, {mm('h_over_k_longer_than_slot'):.0f} | {mm('dead_false'):.1f} | {100*mm('cells_with_head'):.1f} |")
    w("")
    # g) headroom
    w("## 2g. Forwarder-choice headroom: Oracle − FW and CB-warm − FW (paired PDR, 95% CI), and how k changes it\n")
    w("| test | cell | Oracle−FW k=1 | Oracle−FW k=2 | CB−FW k=1 | CB−FW k=2 | RR+FB−FW k=1 | candidates/decision k=1 | k=2 | FW regret k=1 | k=2 | FW losses fixable by another candidate % (k=1 / k=2) |")
    w("|" + "---|" * 12)
    for t in TESTS:
        for c in CELLS:
            def d(k, m):
                x = paired(P(t, k, c, m, "pdr"), P(t, k, c, "FW", "pdr"))
                return f"{x[0]:+.3f} ±{x[1]:.3f}" if x else "-"
            cand = lambda k: mean_(P(t, k, c, "FW", "h_candidates_mean") or [float("nan")])
            reg = lambda k: mean_(P(t, k, c, "FW", "regret") or [float("nan")])
            fx = lambda k: 100 * mean_(P(t, k, c, "FW", "cf_fixable_of_fail") or [float("nan")])
            w(f"| {t} | {cname(c)} | {d(1,'Oracle')} | {d(2,'Oracle')} | {d(1,'CB-warm')} | {d(2,'CB-warm')} | {d(1,'RR+FB')} | {cand(1):.2f} | {cand(2):.2f} | {reg(1):.3f} | {reg(2):.3f} | {fx(1):.1f} / {fx(2):.1f} |")
    w("")
    open(os.path.join(a.outroot, "report_k1.md"), "w").write("\n".join(L))

    # ---------------------------------------------------------------- summary table (Step 3)
    L = []
    w = L.append
    w("# Summary: PDR by test × k (mean of the two cells; each cell 20 paired seeds), and the top-3 loss causes of FW\n")
    w("| test | k | RR | RR+FB | FW | CB-warm | Oracle | FW top-3 loss causes (share of created packets lost / failed attempts) |")
    w("|---|---|---|---|---|---|---|---|")
    for t in TESTS:
        for k in KS:
            vals = []
            for m in METHODS:
                xs = [mean_(P(t, k, c, m, "pdr") or [float("nan")]) for c in CELLS]
                vals.append(f"{mean_(xs):.3f}")
            # top-3 causes, from failed-attempt shares and packet-loss rows
            names = [("PER", "why_per"), ("DATA–DATA collision", "why_col_data"), ("beacon/Hello collision", "why_col_beacon"), ("ACK collision", "why_col_ack"),
                     ("half-duplex", "why_half"), ("receiver dead", "why_dead"), ("out of range", "why_range")]
            sh = sorted(((mean_([mean_(P(t, k, c, "FW", key) or [0.0]) for c in CELLS]), n) for n, key in names), reverse=True)[:3]
            pk = [("MAX_TX drops", "drop_max_tx_reached"), ("TTL drops", "drop_ttl_expired"), ("void/no head", "drop_no_eligible_forwarder"),
                  ("in flight", "in_flight")]
            ps = sorted(((mean_([mean_(P(t, k, c, "FW", key) or [0.0]) for c in CELLS]), n) for n, key in pk), reverse=True)[:1]
            w(f"| {t} | {k} | " + " | ".join(vals) + " | " + ", ".join(f"{n} {100*v:.0f}%" for v, n in sh) + f" of failed attempts; packets: {ps[0][1]} {100*ps[0][0]:.1f}% of created |")
    w("")
    # per cell
    w("## Same, per cell\n")
    w("| test | k | cell | RR | RR+FB | FW | CB-warm | Oracle |")
    w("|---|---|---|---|---|---|---|---|")
    for t in TESTS:
        for k in KS:
            for c in CELLS:
                w(f"| {t} | {k} | {cname(c)} | " + " | ".join(fmt(*mci(P(t, k, c, m, 'pdr'))) if P(t, k, c, m, 'pdr') else "-" for m in METHODS) + " |")
    w("")
    # factor attribution: paired main effects on FW, RR+FB, RR, CB, Oracle
    w("## Which factor matters most? Paired differences in PDR (mean over cells; 95% CI over the 40 cell-seed pairs)\n")
    w("Channel = calibrated − ideal (T3−T1, T4−T2); cell size = new − old (T2−T1, T4−T3); heads = k2 − k1 (all four tests). Averaged over the other factors.\n")
    w("| method | channel (calibrated − ideal) | cell size (750 m − 433 m) | heads (k=2 − k=1) |")
    w("|---|---|---|---|")
    for m in METHODS:
        def eff(pairs):
            d = []
            for (t1, k1), (t2, k2) in pairs:
                for c in CELLS:
                    x, y = P(t2, k2, c, m, "pdr"), P(t1, k1, c, m, "pdr")
                    if x and y:
                        d += [p - q for p, q in zip(x, y)]
            if not d:
                return "-"
            mm, hh = mci(d)
            return f"{mm:+.3f} ±{hh:.3f}"
        ch = [(("T1", k), ("T3", k)) for k in KS] + [(("T2", k), ("T4", k)) for k in KS]
        cs = [(("T1", k), ("T2", k)) for k in KS] + [(("T3", k), ("T4", k)) for k in KS]
        hd = [((t, 1), (t, 2)) for t in TESTS]
        w(f"| {m} | {eff(ch)} | {eff(cs)} | {eff(hd)} |")
    w("")
    open(os.path.join(a.outroot, "summary_k1.md"), "w").write("\n".join(L))
    print("\n".join(L))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("part", choices=["fit", "run", "extra", "report"])
    ap.add_argument("binary")
    ap.add_argument("outroot")
    ap.add_argument("--jobs", type=int, default=4)
    x = ap.parse_args()
    os.makedirs(x.outroot, exist_ok=True)
    {"fit": fit, "run": main_runs, "extra": extra_runs, "report": report}[x.part](x)
