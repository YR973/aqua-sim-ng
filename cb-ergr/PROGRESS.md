# PROGRESS (resume from here)

## Environment
- Workspace: `/home/user/ws/ns-3-dev` (ns-3.41, official git tag; the tarball is blocked here). `src/aqua-sim-ng` -> this repo, `contrib/cb-ergr` -> `cb-ergr/`. `cb-ergr/scripts/setup_ns3.sh` recreates it.
- Build: `cd ns-3-dev && cmake --build cmake-cache -j4` (or `./ns3 build`). Tests: `./build/utils/ns3.41-test-runner-debug --test-name=cb-ergr --verbose` (23 cases, all pass).
- Run: `./build/contrib/cb-ergr/ns3.41-cb-ergr-scenario-debug --ConfigFile=... --OutDir=...` (see CODE-GUIDE §5).
- Missing inputs: the pilot `cb_ergr_pilot.py` was never supplied (link model and current field are from the spec formulas / a stand-in, CONFIRM E8, E10). The design doc v5.2 was given only as chat text; it is not in `docs/` yet.
- The four upstream files we patch use CRLF line endings; edit them with a script that preserves them.

## Done
- D0 environment, layout, params header, docs.
- D1 learner maths (pure C++) + tests 1-5, 7, 8 + sharing/handover tests.
- D2 SNR-based channel, shadowing, link model, patches P1-P4; simulated PER matches the analytic curve (`results/reference/`).
- D3 ERGR-EMHC-g + RoundRobin: grid, beacons, k=2 rank-slotted election, Hmin, member hand-off, void NACK, drifting mobility, localisation noise, metrics + CSVs (summary, nodes, packets, events, frames), consistency checks, scenario runner, determinism test, header tests (test 9), P5 half-duplex patch.

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

## Next (D4 Feedback)
`FeedbackManager` (pending table, ACK timer -> label 0, receipt bitmaps on beacons, expiry, retries with fresh hopSeq, duplicate filter), ACK frames, ground-truth callback (not visible to the learner), `links.csv`, ACK/retransmission columns of `nodes.csv`, `decisions.csv`; tests 12, 13 and the consistency checks for attempts. Then D5 `SelectorCbV5`.

## Open
- See `doc/CONFIRM.md` (A1-A13, B1-B7, C1-C3, E1-E16). Most urgent for the supervisor: Q4 (E0, load, Rx charging), Q1-Q2 (P0 cell size and election), published reference numbers for test 16.
