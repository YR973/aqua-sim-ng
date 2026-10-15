# cb-ergr — CB-ERGR v5 on ns-3 / Aqua-Sim NG

ns-3 module for the MSc thesis of Yousef Roudani (SQU, Dept. of Computer
Science; supervisor Prof. Abderezak Touzene). It implements ERGR-EMHC with a
pluggable forwarder selector, and CB-ERGR v5 (a discounted logistic
contextual bandit with Laplace–Thompson sampling, energy pricing, delayed
feedback and in-cell model sharing) as one of the selectors.

The module lives in this Aqua-Sim NG repository but is built as a separate
ns-3 module in `contrib/cb-ergr`, depending on `aqua-sim-ng`.

## Build

```
cb-ergr/scripts/setup_ns3.sh /path/to/workspace
```

This clones ns-3.41, links this repository to `src/aqua-sim-ng` and
`cb-ergr/` to `contrib/cb-ergr`, then configures and builds.

## Status

Work proceeds one milestone at a time (spec §15), with a review after each.

| Milestone | Content | Status |
|---|---|---|
| M0 | Environment, module skeleton, CONFIRM.md, PATCHES.md | done |
| M1 | Preamble patch, link model, shadowing, energy settings | — |
| M2 | Per-packet transmit power (gate) | — |
| M3 | ERGR-EMHC + RoundRobin, beacons, elections, mobility | — |
| M4 | Feedback manager, headers, receipts, ACKs, CB-lin | — |
| M5 | CbV5 core | — |
| M6 | Power + lifetime price | — |
| M7 | Sharing, handover, known-change inflation | — |
| M8 | AckMode Gated / BitmapOnly | — |
| M9 | Baselines B2–B4, A1, oracles | — |
| M10 | Sweeps and analysis | — |

Open questions and the defaults used are in `doc/CONFIRM.md`; changes to
upstream Aqua-Sim NG files are in `doc/PATCHES.md`.
