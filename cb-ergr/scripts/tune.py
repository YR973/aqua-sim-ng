#!/usr/bin/env python3
"""Tune FW's nine weights or CB's hyperparameters with the SAME optimiser, evaluation budget, seeds and objective.

Usage:
  tune.py --target fw|cb --binary BIN --out DIR [--budget 40] [--seeds 101,102,103,104,105] [--opt-seed 1]
          [--rate 0.004] [--n 100] [--sim 2500] [--warmup 500] [--e0 10000] [--speed 1.0] [--jobs 4]

Why this exists (review R5 / user instruction of 6 Nov 2026): the hand-set FW weights were never tuned while CB's
hyperparameters were hand-set too, so "CB vs FW" compared two untuned rules. Now each side gets the same number of
objective evaluations (--budget), each evaluation being one simulation per training seed.

Method: a (1+1) evolution strategy with the 1/5th success rule. Start = the default point (FW: the physics weights;
CB: the defaults of cb-ergr-params.h, read from scripts/params.json). Objective = mean PDR over the tuning seeds
(common random numbers: every candidate sees the same seeds, so differences between candidates are paired).
Only training seeds 101-110 are allowed; the default tuning seeds are 101-105 and 106-110 stay for validation.

Output: DIR/log.csv (every evaluation), DIR/best.txt, and (with --write-config PATH) a config file with
    SelectorAttributes = ...
that run_sweep.py / compare_smoke.py can use.
"""
import os as _os_nr; _os_nr.environ.setdefault("CB_ERGR_NUMBERED_RUNS", "0")  # sweeps need fixed result paths (run_sweep, tune, ... read OutDir/summary.csv)
import argparse
import csv
import json
import math
import os
import random
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
PARAMS = json.load(open(os.path.join(HERE, "params.json")))


def param(group, name):
    return float(PARAMS[group][name])


class Space:
    """A parameter space: encode/decode between the optimiser's real vector and selector attributes."""

    def __init__(self, target):
        self.target = target
        if target == "fw":
            self.selector = "FixedWeight"
            self.start = [float(v) for v in PARAMS["selector"]["kFwWeights"]]
            self.names = [f"w{i + 1}" for i in range(9)]
            self.scale = [max(0.5, abs(v)) for v in self.start]  # mutation scale per coordinate
            self.lo = [-30.0] * 9
            self.hi = [30.0] * 9
        else:
            self.selector = "CbV5"
            # (attribute, transform, lo, hi, start). 'log': optimise log(value); 'log1m': optimise log(1 - value).
            self.spec = [
                ("ExplorationScale", "log", 0.2, 1.0, param("learner", "kExplorationScale")),   # range 0.2-1.0 since 5 Nov 2026 (was 0.05-4)
                ("Gamma", "log1m", 0.80, 0.999, param("learner", "kForgetGamma")),
                ("Lambda0", "log", 0.05, 20.0, param("learner", "kLambda0")),
                ("KappaP", "lin", 0.0, 1.0, param("learner", "kKappaP")),
            ]
            self.names = [s[0] for s in self.spec]
            self.start, self.lo, self.hi, self.scale = [], [], [], []
            for _, tr, lo, hi, v in self.spec:
                enc = self._enc(tr, v)
                self.start.append(enc)
                self.lo.append(self._enc(tr, lo) if tr != "log1m" else self._enc(tr, hi))
                self.hi.append(self._enc(tr, hi) if tr != "log1m" else self._enc(tr, lo))
                self.scale.append(0.7 if tr != "lin" else 0.25)

    @staticmethod
    def _enc(tr, v):
        return math.log(v) if tr == "log" else math.log(1.0 - v) if tr == "log1m" else v

    @staticmethod
    def _dec(tr, e):
        return math.exp(e) if tr == "log" else 1.0 - math.exp(e) if tr == "log1m" else e

    def attributes(self, x):
        if self.target == "fw":
            return "Weights=" + ",".join(f"{v:.6g}" for v in x)
        return ";".join(f"{name}={self._dec(tr, e):.6g}" for (name, tr, _, _, _), e in zip(self.spec, x))

    def readable(self, x):
        return self.attributes(x)


def evaluate(space, x, args, tag):
    """Mean PDR over the tuning seeds for the point x (runs in parallel)."""
    attrs = space.attributes(x)

    def one(seed):
        out = os.path.join(args.out, "runs", f"{tag}_s{seed}")
        cmd = [args.binary] + (([f"--ConfigFile={args.config}"]) if args.config else []) + \
              [f"--Selector={space.selector}", f"--SelectorAttributes={attrs}", f"--Seed={seed}",
               f"--OutDir={out}", "--LogLinks=false", "--LogNodes=false", "--LogPackets=false", "--LogEvents=false",
               "--ComputeRegret=false"] + args.extra.split()
        if not args.config:   # old-style invocation: explicit scenario numbers
            cmd += [f"--N={args.n}", f"--SimTimeS={args.sim}", f"--WarmupS={args.warmup}", f"--PktRatePerNodeHz={args.rate}",
                    f"--InitialEnergyJ={args.e0}", f"--CurrentSpeed={args.speed}"]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(f"run failed ({tag}, seed {seed}): {r.stderr[-300:]}")
        with open(os.path.join(out, "summary.csv")) as f:
            row = next(csv.DictReader(f))
        return float(row["pdr"])

    with ThreadPoolExecutor(args.jobs) as pool:
        pdrs = list(pool.map(one, args.seed_list))
    return sum(pdrs) / len(pdrs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", choices=["fw", "cb"], required=True)
    ap.add_argument("--binary", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--budget", type=int, default=40, help="number of objective evaluations, including the start point")
    ap.add_argument("--seeds", default="101,102,103,104,105")
    ap.add_argument("--opt-seed", type=int, default=1)
    ap.add_argument("--rate", type=float, default=0.004)
    ap.add_argument("--n", type=int, default=100)
    ap.add_argument("--sim", type=float, default=2500)
    ap.add_argument("--warmup", type=float, default=500)
    ap.add_argument("--e0", type=float, default=10000)
    ap.add_argument("--speed", type=float, default=1.0)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--write-config", default="")
    ap.add_argument("--config", default="", help="scenario config file (e.g. configs/g-base.txt); then --n/--sim/... are not passed")
    ap.add_argument("--extra", default="", help="extra scenario arguments, e.g. '--N=100 --NumSources=10'")
    args = ap.parse_args()
    args.seed_list = [int(s) for s in args.seeds.split(",")]
    if any(not (101 <= s <= 110) for s in args.seed_list):
        sys.exit("tuning is only allowed on the training seeds 101-110")
    os.makedirs(os.path.join(args.out, "runs"), exist_ok=True)

    space = Space(args.target)
    rng = random.Random(args.opt_seed)
    x = list(space.start)
    sigma = 0.3
    best = evaluate(space, x, args, "e000")
    log = [(0, best, best, sigma, space.readable(x))]
    print(f"[{args.target}] eval 0 (start): {best:.4f}  {space.readable(x)}", flush=True)
    for k in range(1, args.budget):
        cand = [min(hi, max(lo, xi + sigma * sc * rng.gauss(0, 1)))
                for xi, sc, lo, hi in zip(x, space.scale, space.lo, space.hi)]
        value = evaluate(space, cand, args, f"e{k:03d}")
        improved = value > best
        if improved:
            x, best = cand, value
            sigma *= 1.5
        else:
            sigma *= 1.5 ** (-0.25)  # 1/5th rule: expand after a success, shrink slowly after failures
        sigma = min(max(sigma, 0.02), 1.5)
        log.append((k, value, best, sigma, space.readable(cand)))
        print(f"[{args.target}] eval {k}: {value:.4f}  best {best:.4f}  sigma {sigma:.3f}", flush=True)

    with open(os.path.join(args.out, "log.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["eval", "pdr", "best_so_far", "sigma_after", "attributes"])
        w.writerows(log)
    final = space.attributes(x)
    with open(os.path.join(args.out, "best.txt"), "w") as f:
        f.write(f"{final}\n")
    print(f"best on tuning seeds {args.seeds}: {best:.4f}\n{final}")
    if args.write_config:
        with open(args.write_config, "w") as f:
            f.write(f"# {args.target.upper()} tuned by scripts/tune.py on training seeds {args.seeds}, budget {args.budget} "
                    f"evaluations, N={args.n}, {args.sim:g} s, rate {args.rate}, E0 {args.e0:g}, speed {args.speed}; "
                    f"config {args.config or '-'} extra '{args.extra}'; do not edit by hand.\nSelectorAttributes = {final}\n")


if __name__ == "__main__":
    main()
