#!/usr/bin/env python3
"""Step 4a/b of the 7 Nov 2026 request: analytic operation counts, MCU time estimates and learner memory.

Usage: opcount.py [--d 9] [--K 6] [--M 4] [--sources 1] [--pending 12]

The counts come from reading the code (model/cb-learner-model.h, small-matrix.h, selector-*.cc, cb-features.cc); every line below is one
call path with its multiply-adds (MAC), adds, divides, square roots, exp/log/pow calls and random draws. 'K' = eligible candidates,
'd' = features, 'L' = power levels (1: power arm off), 'S' = shared-evidence sources held by a head (k - 1 = 1 with two heads per cell),
'M' = extra posterior draws of the ThompsonDisagree gate.

MCU estimate: a Cortex-M4 at 80 MHz. ASSUMPTIONS (typical published cycle counts, not measured here):
  with FPU (M4F, single precision hardware): MAC 4 cycles including loads, add 3, div 14, sqrt 14, exp/log 110, pow 260, uniform draw 12,
  normal draw 300 (polar method: ~2.5 uniforms + log + sqrt + div);
  without FPU (soft float, libgcc): MAC 110, add 50, div 190, sqrt 700, exp/log 2600, pow 6200, uniform draw 40, normal draw 6000.
The learner would run in float32 on the MCU; the simulator uses double. Whether a float32 Cholesky of the 9x9 precision stays positive definite
is not tested here (the ridge lambda0 = 1 on the diagonal makes it likely, but it is an assumption).
"""
import argparse

COST = {
    "fpu":   {"mac": 4,   "add": 3,  "div": 14,  "sqrt": 14,  "exp": 110,  "pow": 260,  "unif": 12, "norm": 300},
    "nofpu": {"mac": 110, "add": 50, "div": 190, "sqrt": 700, "exp": 2600, "pow": 6200, "unif": 40, "norm": 6000},
}
CLK_HZ = 80e6


def chol(d):
    mac = sum(i * (i + 1) // 2 for i in range(d))      # sum over rows of the inner products
    return dict(mac=mac, sqrt=d, div=d * (d - 1) // 2)


def tri(d):
    return dict(mac=d * (d - 1) // 2, div=d)


def add(*ops):
    out = {}
    for o in ops:
        for k, v in o.items():
            out[k] = out.get(k, 0) + v
    return out


def scale(o, f):
    return {k: v * f for k, v in o.items()}


def feature(d):
    # FeatureBuilder::Build with the measured-SNR margin: ~45 mult, ~40 add, 2 sqrt, 7 div, no transcendental
    return dict(mac=45, add=40, sqrt=2, div=7)


def sigmoid():
    return dict(exp=1, div=1)


def dot(d):
    return dict(mac=d)


def refactor(d, S):
    return add(chol(d), dict(pow=S, add=d * S))      # EffectivePrecision: S aged diagonals, each with one pow


def cb_select(d, K, L, S, M, gate):
    ops = add(
        dict(pow=1, mac=2 * d * d, add=d * d + d),                 # BeginDecision: exponent, Blend, theta decay
        refactor(d, S),
        dict(norm=d), tri(d), dict(mac=d),                          # draw z, SampleTheta (back-solve, axpy)
        scale(add(feature(d), dot(d), sigmoid(), dot(d), sigmoid(), dict(pow=1, mac=3, div=1)), K * L),  # per option: features, sample p, mean p, battery term, score
        tri(d), dict(mac=d, sqrt=1),                                 # Width of the chosen option
        dict(unif=1),                                                # audit draw
    )
    if gate:
        per_draw = add(dict(norm=d), tri(d), dict(mac=d),
                       scale(add(dot(d), sigmoid(), dict(pow=1, mac=3, div=1)), K * L))
        ops = add(ops, scale(per_draw, M))
    return ops


def cb_sent(d, S):
    return add(dot(d), sigmoid(), dict(mac=d * d, add=d * d), refactor(d, S))


def cb_label(d):
    return add(dot(d), sigmoid(), dict(mac=d), scale(tri(d), 2), dict(mac=d))


def fw_select(d, K):
    return scale(add(feature(d), dot(d), sigmoid(), dict(mac=3, add=2)), K)


def ql_select(K):
    return scale(dict(add=3, div=0), K)             # table lookup per candidate (map find, compare)


def cycles(ops, kind):
    c = COST[kind]
    return sum(c.get(k, 0) * v for k, v in ops.items())


def show(name, ops):
    f, n = cycles(ops, "fpu"), cycles(ops, "nofpu")
    keys = ["mac", "add", "div", "sqrt", "exp", "pow", "unif", "norm"]
    # exp counts include log calls in pow; report exp, pow and random draws separately
    cells = " | ".join(str(int(ops.get(k, 0))) for k in keys)
    return "| %s | %s | %.0f | %.2f | %.0f | %.1f |" % (name, cells, f, f / CLK_HZ * 1e3, n, n / CLK_HZ * 1e3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--d", type=int, default=9)
    ap.add_argument("--K", type=float, default=6)
    ap.add_argument("--M", type=int, default=4)
    ap.add_argument("--sources", type=int, default=1)
    ap.add_argument("--pending", type=int, default=12)
    a = ap.parse_args()
    d, K, S, M = a.d, int(round(a.K)), a.sources, a.M
    L = ["Operation counts per call (d = %d features, K = %d candidates, L = 1 power level, S = %d shared sources, M = %d)" % (d, K, S, M), "",
         "| call | MAC | add | div | sqrt | exp | pow | uniform draws | normal draws | cycles (FPU) | ms @80 MHz (FPU) | cycles (soft float) | ms @80 MHz (soft float) |",
         "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    rows = [
        ("CB Select (Logit/ProbStd gate)", cb_select(d, K, 1, S, M, False)),
        ("CB Select + ThompsonDisagree (M draws)", cb_select(d, K, 1, S, M, True)),
        ("CB OnSent (send evidence + refactor)", cb_sent(d, S)),
        ("CB OnLabel (one Newton step)", cb_label(d)),
        ("FW Select", fw_select(d, K)),
        ("FW OnLabel", {}),
        ("QL Select", ql_select(K)),
        ("QL OnLabel", dict(mac=1, add=2)),
        ("CARP-EWMA OnLabel", dict(mac=2, add=1)),
        ("RR Select", dict(add=1)),
    ]
    for name, ops in rows:
        L.append(show(name, ops))
    L += ["", "Totals for one forwarded packet with one ACK-labelled attempt (Select + OnSent + OnLabel):", ""]
    cbt = add(cb_select(d, K, 1, S, M, False), cb_sent(d, S), cb_label(d))
    cbt_td = add(cb_select(d, K, 1, S, M, True), cb_sent(d, S), cb_label(d))
    L += ["| method | cycles (FPU) | ms (FPU) | cycles (soft) | ms (soft) |", "|---|---|---|---|---|"]
    for name, ops in [("CB", cbt), ("CB + ThompsonDisagree", cbt_td), ("FW", fw_select(d, K)), ("QL", add(ql_select(K), dict(mac=1, add=2)))]:
        L.append("| %s | %.0f | %.2f | %.0f | %.1f |" % (name, cycles(ops, "fpu"), cycles(ops, "fpu") / CLK_HZ * 1e3, cycles(ops, "nofpu"), cycles(ops, "nofpu") / CLK_HZ * 1e3))
    # memory
    L += ["", "Learner memory per head (float32 on the MCU, double in the simulator):", "",
          "| item | floats | MCU bytes (float32) | simulator bytes (double) |", "|---|---|---|---|"]
    items = [("theta (posterior mean)", d), ("theta_cell (cell prior)", d), ("A (own precision, symmetric half)", d * (d + 1) // 2),
             ("L (cached Cholesky factor, lower half)", d * (d + 1) // 2), ("shared evidence per co-head (diag d + timestamp) x S", S * (d + 1)),
             ("clock, last discount, lambda_E, counters", 6)]
    tot = 0
    for n, f in items:
        L.append("| %s | %d | %d | %d |" % (n, f, 4 * f, 8 * f))
        tot += f
    L.append("| **total model state** | %d | **%d** | %d |" % (tot, 4 * tot, 8 * tot))
    pend = 9 * 4 + 12
    L.append("| one pending-label record (features d floats + fwd 2 B + hopSeq 2 B + packet key 4 B + send time 4 B + flags 2 B) | - | %d | see sizeof_pending_attempt_bytes in cost.csv |" % pend)
    L.append("| pending records at the measured peak (%d) | - | %d | - |" % (a.pending, a.pending * pend))
    L.append("")
    L.append("CB keeps no per-candidate state: one model per head, shared by all candidates (the candidates' features come from the neighbour table). "
             "FW keeps nothing; QL keeps one value per neighbour (4 B each); CARP-EWMA one value per neighbour (4 B each).")
    print("\n".join(L))


if __name__ == "__main__":
    main()
