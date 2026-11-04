# PROGRESS (resume from here)

## Environment
- Workspace: `/home/user/ws/ns-3-dev` (ns-3.41, official git tag; the tarball is blocked here). `src/aqua-sim-ng` -> this repo, `contrib/cb-ergr` -> `cb-ergr/`. `cb-ergr/scripts/setup_ns3.sh` recreates it.
- Build: `cd ns-3-dev && cmake --build cmake-cache -j4` (or `./ns3 build`). Tests: `./build/utils/ns3.41-test-runner-debug --test-name=cb-ergr --verbose` (46 cases, all pass).
- Run: `./build/contrib/cb-ergr/ns3.41-cb-ergr-scenario-debug --ConfigFile=... --OutDir=...` (see CODE-GUIDE §5).
- Missing inputs: the pilot `cb_ergr_pilot.py` was never supplied (link model and current field are from the spec formulas / a stand-in, CONFIRM E8, E10). The design doc v5.2 was given only as chat text; it is not in `docs/` yet.
- The four upstream files we patch use CRLF line endings; edit them with a script that preserves them.

## Done
- D0 environment, layout, params header, docs.
- D1 learner maths (pure C++) + tests 1-5, 7, 8 + sharing/handover tests.
- D2 SNR-based channel, shadowing, link model, patches P1-P4; simulated PER matches the analytic curve (`results/reference/`).
- D3 ERGR-EMHC-g + RoundRobin: grid, beacons, k=2 rank-slotted election, Hmin, member hand-off, void NACK, drifting mobility, localisation noise, metrics + CSVs (summary, nodes, packets, events, frames), consistency checks, scenario runner, determinism test, header tests (test 9), P5 half-duplex patch.

- D4 feedback: `FeedbackManager` (pending table, ACK timer -> label 0 at once, receipt bitmaps, expiry, retries with fresh hopSeq), ACK frames, ground truth in the metrics only, `links.csv`, `decisions.csv`, label statistics, attempt consistency check; tests 12 and 13. First feedback numbers (RR+FB, N=100, 0.004 pkt/s, 1500 s, seed 1): PDR 0.44 vs 0.27-0.29 without; 5.6 attempts per packet; labels 22% by ACK, 78% by ACK timeout; ground-truth hop success 0.35; false-negative rate 0.16; label delay mean 2.5 s. Label 1 was never wrong in the line test (58 of 58 truly received).

- D5 `SelectorCbV5`: features, Thompson sampling, score, ACK gate, send-time evidence, label update, dual price timer, blob sharing; tests 6 + selector behaviour. Smoke comparison on training seeds (N=100, E0=10000 J, 1 m/s): PDR 0.520 (CB) vs 0.483 (RR+FB) vs 0.288 (RR) at 0.004 pkt/s, and 0.202 vs 0.167 vs 0.153 at 0.02 pkt/s; CB wins on every seed. The ACK gate and sharing showed no effect at this scale (CONFIRM E21). Details in CHANGELOG.

- D6a baselines FW, QL, CARP, LinQ (+ shared `FeatureBuilder`). **Key finding:** hand-set fixed weights (FW) match or beat CB at N=100 (PDR 0.552 vs 0.520 over 2500 s; 0.527 vs 0.532 over 12500 s), so CB's gain over RR+FB is mostly the features, not the online learning (CONFIRM E22). QL 0.435 and LinQ 0.462 are below RR+FB-with-features.

- D6b oracles, regret, A1. Held-out training seeds 108-110: O 0.641, FW 0.584, A1 0.563, CB 0.534, RR+FB 0.475 PDR; regret 0, 0.069, 0.081, 0.146, 0.190. A static rule on the features beats the online learner at this scale and horizon.

- D7a failure handling: suspect tier, HANDOVER frames and planned/unplanned takeover, failure injection (`--KillHeadsAtS=8000 --KillHeadsCells=5`), spec test 14 and two integration tests. Recovery-time analysis still to write (`analyse.py`).

- D7b `--LeanBeacons=true` / `--HeaderFirstRx=true`: with both, at N=250, 300 J, 0.004 pkt/s the first node dies at 1795 s (was 1036 s) and half the nodes survive 3000 s (was dead at 1789 s); Rx is still 86% of the energy, so E0 must still be agreed (CONFIRM E13).

- D7c per-frame power (patch P8, tag, test 11 passes). Power ablation: lowers Tx energy 40-50% but halves PDR (0.521 -> 0.249 at 0.004 pkt/s); main results should stay forwarder-only.

## Calibration findings (D3 deliverable; RoundRobin, seed 1, training-style runs, no tuning)
| Run | PDR | Rx share of energy | First node dead | Half nodes dead |
|---|---|---|---|---|
| N=250, 0.07 pkt/s, 2000 s | 0.035 | 0.904 | 672 s | 872 s |
| N=250, 0.004 pkt/s | 0.139 | 0.908 | 1036 s | 1781 s |
| N=250, no data (beacons only) | - | 0.903 | 1287 s | none by 2000 s |
| N=100, 0.004 pkt/s, 1500 s | 0.29 | 0.85 | none | none |
- Rx energy is ~90% of the total, so by the D§4.1 rule N-A (header-first receive) and N-C (lean beacons) are **must**, and E0 has to be set (or the published one confirmed) with the supervisor: with 300 J the network is dead in 11-21 minutes.
- At 0.07 pkt/s the channel is saturated (PDR 3.5% even before deaths). The pilot's 0.004 pkt/s is workable.
- Round robin PDR is low (15-30%) because the shadowed links are poor (CONFIRM E16) and there are no retransmissions; it is the baseline CB must beat.
- **Spec test 16 (PDR falls as speed rises) is NOT reproduced**: PDR is flat in speed (0.272, 0.287, 0.294, 0.294 for U = 0, 0.5, 1, 2). Mobility does raise elections/hour (116 -> 486), shorten head tenure (1242 s -> 350 s) and raise voids (7 -> 24). Not tuned. Needs the published reference numbers (CONFIRM E15, C3).
- Speed: N=250 takes ~35 s of wall time per 2000 s in the debug build (about 6 min for 20000 s); N=100 takes ~7 s per 1000 s.

## Next
1. `run_sweep.py` (YAML or JSON list -> parallel runs, skips existing outputs) and `analyse.py` (mean +- 95% CI over seeds, ablation table, recovery time from `events.csv` + `decisions.csv`).
2. (done) N-A and N-C. Next for energy: agree E0 / Rx power with the supervisor; optionally N-B (members sleep).
3. (done) Power patch and test 11.
4. Optional: CB warm start from the A1 weights (design P7), because static rules beat the cold-start learner over 2500 s.
Run the 20-seed comparisons only on the test seeds 1-20 and only after the settings are frozen on training seeds 101-110.

## Open
- See `doc/CONFIRM.md` (A1-A13, B1-B7, C1-C3, E1-E16). Most urgent for the supervisor: Q4 (E0, load, Rx charging), Q1-Q2 (P0 cell size and election), published reference numbers for test 16.
