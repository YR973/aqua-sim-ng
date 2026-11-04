#!/usr/bin/env python3
"""Run a sweep of cb-ergr-scenario simulations in parallel (kickoff §4 D6, spec §12).

Usage:  run_sweep.py SWEEP.yaml [--jobs 3] [--dry-run] [--allow-test-seeds]

The YAML file lists what to run; every (run, sweep value, seed) combination gets its own directory
    OUT_ROOT/<run>/<factor>=<value>/seed<seed>/        (summary.csv, nodes.csv, ... written by the simulator)
and is SKIPPED if its summary.csv already exists, so an interrupted sweep can simply be started again.

    binary: build/contrib/cb-ergr/ns3.41-cb-ergr-optimized     # the scenario executable
    out_root: results/sweep1
    seeds: [101, 102, 103]            # training seeds 101-110; test seeds are 1-20
    common: {N: 250, SimTimeS: 20000, WarmupS: 2000}         # settings for every run (any --Name=value of the scenario)
    runs:                             # the methods / ablation rows
      - {name: RR,  selector: RoundRobin}
      - {name: CB,  selector: CbV5, attributes: "AckMode=Gated", settings: {LeanBeacons: true}}
    sweeps:                           # one factor at a time (spec §12); omit for a single point
      - {factor: CurrentSpeed, values: [0, 0.5, 1, 2]}

Seeds 1-20 are the TEST seeds: use them only for final results, after every setting is frozen on training seeds
101-110 (kickoff §5). The script refuses them unless --allow-test-seeds is given.
"""
import argparse
import itertools
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import yaml


def build_jobs(cfg):
    sweeps = cfg.get("sweeps") or [{"factor": None, "values": [None]}]
    jobs = []
    for run in cfg["runs"]:
        for sweep in sweeps:
            for value in sweep["values"]:
                for seed in cfg["seeds"]:
                    factor = sweep["factor"]
                    point = "base" if factor is None else f"{factor}={value}"
                    out = os.path.join(cfg["out_root"], run["name"], point, f"seed{seed}")
                    settings = dict(cfg.get("common") or {})
                    settings.update(run.get("settings") or {})
                    if factor is not None:
                        settings[factor] = value
                    settings["Seed"] = seed
                    settings["Selector"] = run["selector"]
                    if run.get("attributes"):
                        settings["SelectorAttributes"] = run["attributes"]
                    settings["OutDir"] = out
                    jobs.append((out, settings))
    return jobs


def command(binary, settings):
    cmd = [binary]
    for key, value in settings.items():
        if isinstance(value, bool):
            value = "true" if value else "false"
        cmd.append(f"--{key}={value}")
    return cmd


def run_job(binary, job):
    out, settings = job
    if os.path.exists(os.path.join(out, "summary.csv")):
        return out, "skipped"
    os.makedirs(out, exist_ok=True)
    result = subprocess.run(command(binary, settings), capture_output=True, text=True)
    if result.returncode != 0:
        with open(os.path.join(out, "FAILED.txt"), "w") as f:
            f.write(result.stdout[-2000:] + "\n" + result.stderr[-4000:])
        return out, "FAILED"
    return out, "done"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sweep")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--allow-test-seeds", action="store_true")
    args = ap.parse_args()

    cfg = yaml.safe_load(open(args.sweep))
    if any(int(s) <= 20 for s in cfg["seeds"]) and not args.allow_test_seeds:
        sys.exit("seeds 1-20 are TEST seeds; pass --allow-test-seeds only for final, frozen runs")
    jobs = build_jobs(cfg)
    print(f"{len(jobs)} runs ({len(cfg['runs'])} methods x sweep points x {len(cfg['seeds'])} seeds)")
    if args.dry_run:
        for out, settings in jobs[:5]:
            print(" ".join(command(cfg["binary"], settings)))
        return
    counts = {"done": 0, "skipped": 0, "FAILED": 0}
    with ThreadPoolExecutor(args.jobs) as pool:
        for out, status in pool.map(lambda j: run_job(cfg["binary"], j), jobs):
            counts[status] += 1
            if status == "FAILED":
                print("FAILED:", out, file=sys.stderr)
    print(counts)


if __name__ == "__main__":
    main()
