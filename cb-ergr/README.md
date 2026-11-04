# cb-ergr — CB-ERGR v5 on ns-3 / Aqua-Sim NG

ns-3 module for the MSc thesis of Yousef Roudani (SQU, Dept. of Computer Science; supervisor Prof. Abderezak Touzene).
It implements the grid protocol ERGR-EMHC with a pluggable forwarder selector (ERGR-EMHC-g), CB-ERGR v5 (a discounted
logistic contextual bandit with Laplace-Thompson sampling, energy pricing, delayed feedback and in-cell model sharing)
and the baselines and oracles it is compared with.

Start here: `doc/CODE-GUIDE.md` (how to read, run and change the code), `PROGRESS.md` (state and next steps),
`doc/CHANGELOG.md` (every change with its reason), `doc/CONFIRM.md` (assumptions awaiting the supervisor),
`doc/PATCHES.md` (the guarded changes to upstream Aqua-Sim NG files). Source documents are in `docs/`.

## Build and run

```
cb-ergr/scripts/setup_ns3.sh /path/to/workspace          # clones ns-3.41, links this repo, builds
./build/contrib/cb-ergr/ns3.41-cb-ergr-scenario-debug --ConfigFile=configs/smoke.txt --Selector=CbV5 --OutDir=results/x
./build/utils/ns3.41-test-runner-debug --test-name=cb-ergr --verbose
```

## What exists

Substrate: 3D grid, rank-slotted election with k heads per cell, Hmin wave, member hand-off, void NACKs, suspect tier,
failure injection, planned/unplanned model handover, SNR-based acoustic channel with Gauss-Markov shadowing, drifting
nodes, noisy localisation, optional lean beacons (N-C), header-first receive (N-A) and per-frame transmit power.
Methods (`Selector=`): `RoundRobin` (published ERGR-EMHC; `WithFeedback=true` adds ACK/retries), `CbV5`, `FixedWeight`,
`FittedLogistic` (A1), `QLearning`, `CarpEwma`, `LfaQ`, `Oracle`, `GeoOracle`.
Outputs per run: `summary.csv`, `nodes.csv`, `links.csv`, `packets.csv`, `events.csv`, optional `decisions.csv` and `frames.csv`.
Tools: `scripts/run_sweep.py`, `scripts/analyse.py`, `scripts/fit_a1.py`, `scripts/compare_smoke.py`, `scripts/check_params.py`.

## Status

All steps D0-D7 of the kickoff are built and tested (46 test cases). The results so far are smoke runs on training seeds
(N = 100, 2500 s) and are listed in `doc/CHANGELOG.md` and `results/reference/`. No full-size (N = 250, 20000 s, 20 test seeds)
experiment has been run: that needs the open decisions in `PROGRESS.md` (battery size and load, published reference numbers).
