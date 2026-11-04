# CHANGELOG (newest first)

One entry per change. Format fixed by the kickoff (rule 8).

## 2026-11-04 23:59 — Tuned FW and CB on equal budget, and the three pre-comparison reports
- What: `scripts/tune.py` (one (1+1)-ES with the 1/5th rule for both targets); `configs/fw-tuned.txt`, `configs/cb-tuned.txt`; `FixedWeight` takes an optional `Weights` attribute (empty = the hand-set physics weights); `compare_smoke.py` variants `FW` (tuned), `FW-phys` (the physics weights, second baseline), `CB-tuned`; per-frame-type energy columns in `summary.csv` (`energy_<type>_{tx,rx}_per_delivered_j`); results in `results/reference/tune/`.
- Why: user instruction of 6 Nov: replace the hand-set FW weights by weights tuned on the training seeds under the same budget as CB, keep the physics version as a second baseline, and report three things before comparing CB with FW again.
- Effect on results: `Selector=FixedWeight` without attributes is unchanged (physics weights). The comparison label "FW" now means tuned weights (attribute from `configs/fw-tuned.txt`).
- Spec/design deviation:
  1. **Budget definition.** Equal = 40 objective evaluations each (start point + 39 candidates), each evaluation = 5 simulations (seeds 101-105, N = 100, 2500 s, warm-up 500 s, 0.004 pkt/s, E0 10000 J, speed 1), objective = mean PDR, same optimiser, same seeds (common random numbers), same start rule (the defaults). FW has 9 free numbers, CB has 4 (ExplorationScale, Gamma, Lambda0, KappaP; searched on log scales). Validation seeds 106-110 were never used for tuning. This is one reading of "same budget"; the other (same number of simulated seconds as CB sees online) is not what CB's online learning is about, so it was not used.
  2. **ACK energy is an approximation.** Tx = frames x P_tx x airtime, Rx = frames decoded by a routing layer x P_rx x airtime, ordinary nodes only. The Tx total is about 3-20% above the PHY's own `energy_tx_j` and the Rx total is about half of the PHY's `energy_rx_j` (a node also pays for frames that arrive corrupted or that are never passed up). So the ACK Rx figure is a lower bound; the true ACK share is below ~10%, not above.
- Result (held-out training seeds 106-110, N = 100, 0.004 pkt/s, E0 10000 J, speed 1; `results/reference/tune/`):
  Tuning (seeds 101-105): FW 0.5516 -> 0.5703, CB 0.5186 -> 0.5299 (in-sample PDR).
  | | PDR 2000 s | PDR 10000 s | regret 2000 s | regret 10000 s | ≥2 viable 2000 s | ≥2 viable 10000 s |
  |---|---|---|---|---|---|---|
  | RR+FB | 0.474 | 0.504 | 0.184 | 0.152 | 0.660 | 0.590 |
  | CB (defaults) | 0.522 | 0.535 | 0.133 | 0.104 | 0.691 | 0.615 |
  | CB-tuned | 0.534 | 0.539 | 0.132 | 0.098 | 0.701 | 0.617 |
  | FW-phys | 0.553 | 0.561 | 0.048 | 0.040 | 0.693 | 0.594 |
  | FW (tuned) | 0.557 | 0.567 | 0.047 | 0.040 | 0.686 | 0.596 |
  | A1 | 0.558 | 0.565 | 0.052 | 0.045 | 0.683 | 0.591 |
  | Oracle | 0.591 | 0.586 | 0 | 0 | 0.668 | 0.588 |
  1. **Tuning FW helps nothing out of sample** (FW-phys 0.553 -> tuned 0.557 at 2000 s; 0.561 -> 0.567 at 10000 s; the in-sample gain of +0.019 was noise/overfit), so the hand-set physics weights were already near the best of this rule. Tuning CB's hyperparameters gains +0.012 (2000 s) and +0.004 (10000 s).
  2. **Share of decisions with at least two viable candidates (predicted margin > 0 at full power): 0.66-0.70 at 2000 s and 0.59-0.62 at 10000 s.** So about two thirds of the decisions are real choices; the benchmark is not degenerate. The oracle beats FW by only 0.03 PDR, which is the whole headroom above the static rule.
  3. **ACK energy per delivered packet:** 4.2-5.0 J (Tx 0.47-0.64 J + Rx 3.7-4.4 J, Rx a lower bound) out of 78-99 J, i.e. about 5% (below ~10% with the Rx correction), 3.4-4.1 ACK frames per delivered packet, the same for every method. The ACK gate can therefore save at most a few percent of the total energy; this is why `AckMode=Gated` shows no effect (CONFIRM E21).
  4. **2000 s vs 10000 s:** CB's regret falls from 0.133 to 0.104 (tuned: 0.132 to 0.098) while FW's stays at 0.04-0.05, so CB does learn, but its PDR gain is small (+0.013) and the gap to FW does not close: FW - CB = 0.035 +- 0.020 (2000 s) and 0.032 +- 0.031 (10000 s) (paired difference over 5 seeds, 95% t interval). FW - CB-tuned = 0.023 +- 0.052 and 0.028 +- 0.033. The ordering FW ≈ A1 > CB-tuned > CB > RR+FB holds at both horizons. PDR of the methods with feedback rises with time (nodes learn/neighbour tables settle), the oracle's falls slightly (deaths and fewer viable links).
  Reading: on this benchmark the static logistic rule on CB's own features captures most of what is available (0.557 of the oracle's 0.591); the online learner does not beat it in 10000 s. This is the honest answer to the N1 claim so far (CONFIRM E22).

## 2026-11-04 23:30 — P0: ERGR-EMHC as published, and spec test 16
- What: new class `ErgrEmhcPublishedRouting` (`model/ergr-emhc-published.{h,cc}`, selected by the scenario setting `Protocol=P0`; default `G` = the improved ERGR-EMHC-g, unchanged); `GridGeometry::NeighbourCellsPaper/AreNeighboursPaper/HminTable`; `Exit` election kind; channel attribute `IdealLinks` (scenario setting of the same name); `params::p0` and `params::channel::kIdealLinks`; `configs/p0-published.txt` rewritten; `sweeps/test16-p0.yaml`; 2 tests (46 cases pass in total); `results/reference/test16/`.
- Why: instructions of 6 Nov 2026 (original protocol with cell side R/(2 sqrt 3) = 433 m, NG1T/NG2T tables, GBPR-style random election timers, all values [CONFIRM]) and spec test 16 (trend of PDR against speed, taken from the paper's Fig. 22).
- What P0 does (paper §4, everything else is a `params::p0` value marked [CONFIRM]): 32 neighbour cells; Hmin from the grid (BFS over the 32-neighbour graph, no beacon wave); heads learned from Hello frames only (so tables can be stale by up to the 500 s update period); forwarder = round robin over the not-busy entries of NG1T with Hmin <= Hpkt, Hpkt = round(0.9 Hmin + 0.1 Hmax) with Hmax = 2 Hmin [CONFIRM]; the source writes Hpkt - 1, each forwarder decrements; the previous cell travels in the DATA header (`hopSeq` field); void: NegAck to the previous hop, which re-selects from NG1T + NG2T excluding the previous and already tried cells; no ACKs (the waiting time T_w without a NegAck counts as success); nodes in cells with Hmin 1 send straight to the nearest sink; election: random timer iPeriod x U(0,1) (priority timer iPeriod x P(i) is `RandomTimers=false`), Hello, 500 s update, 400 s new-election timer, Retire below 10 J, Exit when a head leaves its cell; true positions (no localisation noise); no learner (round robin only).
- Effect on results: none on ERGR-EMHC-g (`Protocol=G`). `IdealLinks` defaults to false.
- Spec/design deviation:
  1. **Only a head sends Exit.** The paper says "a node" leaving its cell; with all nodes sending Exit (569 cell changes in 650 s at 1 m/s) the Exit frames collided with data (hop success 63%), and nobody keeps a table entry for a non-head, so the frames carried no information. [CONFIRM]. (Before the change, one seed, 800 s, ideal links, the paper's radio not yet set: PDR 0.31 with hop success 63%; the two changes were not separated.)
  2. **P0 uses the paper's radio** (17.8 kbit/s, 8 W / 0.8 W, 300 J, 3 sinks, 10 sources x 0.07 pkt/s, 2000 s with 150 s warm-up) in `configs/p0-published.txt`, not the spec's 5 kbit/s, 2 W / 0.75 W. The MAC is still the Aqua-Sim broadcast MAC (no carrier sense); the paper used CSMA. This is why the absolute PDR is lower than the paper's.
  3. **`IdealLinks`** makes every frame inside R decodable (loss draw still consumed) to mimic the paper's BER 1e-9 channel; collisions and half-duplex remain. P0 is reported under the calibrated channel and under `IdealLinks` with `ShadowSigmaDb=0`.
  4. `hopSeq` carries the previous cell and `OnAttemptSent` is called without feedback, so `hop_success_truth` still works for P0 (ground truth only).
- Result (test 16, 8 training seeds 101-108, N = 300, 2000 s; mean +- 95% CI; `results/reference/test16/aggregate.csv`):
  | speed (m/s) | paper Fig. 22 | P0, IdealLinks | P0, calibrated channel |
  |---|---|---|---|
  | 0 | 0.875 | 0.616 +- 0.061 | 0.505 +- 0.059 |
  | 0.5 | 0.84 | 0.573 +- 0.043 | 0.466 +- 0.062 |
  | 1.0 | 0.805 | 0.519 +- 0.043 | 0.418 +- 0.063 |
  | 1.5 | 0.752 | 0.484 +- 0.061 | 0.399 +- 0.059 |
  The trend of spec test 16 is reproduced: PDR falls monotonically with speed, by 0.13 (-21%) from 0 to 1.5 m/s under IdealLinks (paper: 0.12, -14%) and by 0.11 (-21%) under the calibrated channel. The adjacent differences are inside the confidence intervals; the end-to-end fall (0.13, against a 95% half-width of 0.06 per point) is clearly larger. The absolute level is 0.26 below the paper's at speed 0: about 18% of the data hops are lost (one seed, speed 0: hop success 0.82, almost all collisions, since the links are ideal) (the broadcast MAC has no carrier sense, all frames reach about half the 3 km box) and 23% of the packets die on the first hop. The paper's CSMA is the likely difference; not tuned.

## 2026-11-06 20:00 — Supervisor-side decisions after reading the ERGR-EMHC paper
- What: decisions recorded; configs `configs/g-base.txt` (new) and an updated `base.txt`; new scenario setting `NumSources`.
- Why: instructions of 6 Nov 2026 plus the two papers supplied (Al-Salti et al., Computer Networks 162 (2019) 106869; the LSWTS/3DUL localisation study, IJCNC 15(2) 2023, which only restates the protocol).
- Decisions:
  1. **Pilot cross-check skipped.** The PER check against the analytic link model (D2, `results/reference/per_sigma*.csv`) is accepted as sufficient; the pilot file is not needed. CONFIRM D1/E8 are closed on that basis. The mobility field stand-in (E10) stays a stand-in.
  2. **ERGR-EMHC-g runs keep E0 = 10000 J and 0.004 pkt/s per node as [CONFIRM] values** (`configs/g-base.txt`).
  3. **P0 runs** use cell side R/(2 sqrt 3) = 433 m, the NG1T/NG2T tables and GBPR-style random election timers, all timer values [CONFIRM] (see the P0 entry below).
  4. **Test 16 takes its trends from the paper's figures** (see CONFIRM E26).
- What the paper changes (reading it settled several open questions):
  * **Traffic is per source node, not per node.** "Traffic injection rate 0.07 packets/s" applies to 10 randomly chosen source nodes among 100-300 nodes (5-15% sources in the load sweep), i.e. 0.7 pkt/s for the whole network. My earlier reading (0.07 pkt/s for all 250 nodes, 17.5 pkt/s total) gave a saturated channel, which explains the 3.5% PDR and the "27 Erlang" of design §4.1. CONFIRM Q4 is answered for the traffic part.
  * **Radio setup of the paper:** 3 x 3 x 3 km, R = 1.5 km, Tx 8 W, Rx 0.8 W, idle 0.008 W, 35.695 kHz, 17.8 kbit/s, BER 1e-9 (so essentially error-free inside the range), CSMA MAC, 3 sinks, 300 J, 2000 s runs with the first and last 150 s discarded, 25 runs, RPGM group mobility (max speed 0-1.5 m/s). The spec's 5 kbit/s, 2 W / 0.75 W and calibrated PER are our own choices; absolute numbers are therefore not comparable with the paper, only trends.
  * **Protocol details for P0:** cell side d = R/(2 sqrt 3); 32 neighbour cells (the 26 adjacent plus 6 at +-2 along one axis); neighbour cells split into G1 (Hmin lower than the own cell's) and G2 (equal or higher); nodes keep NG1T/NG2T tables of the cell-heads of neighbouring cells, learned from Hello packets (Hello carries type, CID, NID, Hmin), plus a Busy flag per entry; Hmin is the cell-level hop count to the nearest sink (a function of the grid, not a beacon wave); Hpkt = beta Hmin + (1 - beta) Hmax with beta = 0.9; forwarder = round robin over G1 entries with Hmin <= Hpkt that are not busy; void handling with NegAck and a waiting time T_w; election priority P(i) = w (1 - E/maxE) + (1 - w) D/maxD with initialisation timer iPeriod x P(i); Exit and Retire packets; Table 3: iPeriod 50 s, update period 500 s, new election 400 s, energy threshold 10 J, beta 0.9.

## 2026-11-06 14:00 — D6c tooling: run_sweep.py, analyse.py, check_params.py; CB warm start; design doc saved
- What: `scripts/run_sweep.py` (YAML, parallel, skips finished runs, refuses test seeds 1-20 unless `--allow-test-seeds`), `scripts/analyse.py` (mean +- 95% CI with Student's t, plots against each sweep factor, ablation table, recovery time and dip depth after failure injection), `scripts/check_params.py` (writes `scripts/params.json` from the header, and fails on stale JSON or on an attribute default that is a literal instead of a `params::` value), `sweeps/example-speed.yaml`; CB attribute `PriorWeights` (warm start, design P7); `docs/cb-ergr-design-final-detailed.md` (verbatim copy of design v5.2); `kRequestAckOnFirstAttempt` in the params header.
- Why: kickoff §4 D6 (sweep + analysis), §0 rule 11 (one home for parameters, consistency check), design P7.
- Effect on results: none on existing runs; new variant CB-warm.
- Spec/design deviation:
  1. **The rule-11 checker found four real violations** (hard-coded `true` for `AckRequest` defaults in four selectors) and they were moved into the params header.
  2. **Recovery time** is pooled over all attempts into the affected cells (per-node curves are too sparse). First version "recovered" at 0 s because the 300 s window still held pre-failure data; it now needs a dip below baseline - 5 points first, reports 0 for "never dipped" and NaN (censored) for "dipped and not yet back". With 3 seeds and 5 affected cells it is exploratory (CB 343 s, CB without sharing 83 s, RR+FB 160 s) and should not be read as a result.
  3. **Warm start** keeps the prior precision at lambda0 (design P7 suggests 0.1 of the Laplace Hessian; not built) and sets both thetaHat and the cell prior to the fitted weights, so forgetting decays toward them.
- Result (held-out training seeds 108-110, N = 100, 2500 s, 0.004 pkt/s; `results/reference/d5-smoke/compare_warmstart_heldout_seeds108-110.csv`): PDR FW 0.571, A1 0.567, CB-warm 0.565, CB cold 0.546, oracle 0.594; regret A1 0.047, CB-warm 0.090, CB cold 0.120. The warm start removes most of the cold-start loss; online refinement on top of the offline fit (CB-warm - A1) is -0.002 PDR here, i.e. not measurable at this scale.

## 2026-11-06 10:00 — D7c per-packet transmit power (patch P8, spec test 11) and the power ablation
- What: `TxPowerLevelTag` (cb-ergr), PHY patch P8 (`SetTxPowerHook`), `CbErgrHelper::EnablePowerLevels`, the channel reads the tag to set the frame's source level (`SL_full + offset[level]`), routing tags DATA frames sent below full power, `InjectRawFrame` test hook; test 11 (46 cases pass).
- Why: kickoff §2.3 / §4 D7 and spec §4.2. The gate passes, so `EnablePower` is a meaningful ablation.
- Effect on results: only when `EnablePower=true` (default false).
- Spec/design deviation:
  1. The power tag is read by the channel, not by the PHY stamp: the upstream PHY has no per-frame level, so the source level goes through the channel subclass and only the energy charge needs the PHY hook (P8). Untagged frames (beacons, ACKs, control) use the full level.
  2. **Test 11 asserts non-decreasing success at 1400 m, and strict monotonicity at 1250 m.** At 1400 m the -3 dB and -6 dB levels have success exactly 0 (BFSK error cliff: PER goes from 0.15 to about 1), so "strictly increasing" cannot hold there; the spec's intent (power matters, in the right direction) is checked at 1250 m where the three levels give about 0 / 0.1 / 0.99. Energy per transmission matches `P_tx[level] x airtime` within 1% and the success rates match the link model within 3.5 points at 500, 1000, 1250 and 1400 m.
- Result (training seeds 101-103, N = 100, 2500 s; `results/reference/d5-smoke/compare_power_rate*.csv`):
  | rate | method | PDR | Tx energy (J) |
  |---|---|---|---|
  | 0.004 | CB (full power) | 0.521 | 3709 |
  | 0.004 | CB + power + lifetime price | 0.249 | 2211 |
  | 0.004 | CB + power, no price | 0.410 | 3023 |
  | 0.02 | CB (full power) | 0.182 | 9978 |
  | 0.02 | CB + power + lifetime price | 0.111 | 4798 |
  | 0.02 | CB + power, no price | 0.153 | 7205 |
  The power arm lowers Tx energy by 40-50% but lowers PDR by half. Reasons: three times as many arms to learn from the same few labels, and a steep error cliff, so a level that is one step too low fails outright and costs a retransmission. Receiving is ~90% of all energy, so the total energy saving is small. Reported as is; the main results are therefore forwarder-only (as K§2.3 intends).

## 2026-11-06 06:00 — D7b energy: N-A header-first receive and N-C lean beacons
- What: PHY patch P7 (`SetRxEnergyTimeHook`: optional per-frame receive-energy duration; no hook = upstream); `CbErgrHelper::EnableHeaderFirstRx/HeaderFirstChargeS`; routing flags `LeanBeacons`, `TrickleMaxFactor`, `PreElectionLeadS`; per-row neighbour expiry that honours the advertised beacon interval; scenario settings `HeaderFirstRx`, `LeanBeacons`, `TrickleMaxFactor`. 3 new tests (45 cases pass).
- Why: kickoff §2.2 (implement behind flags) and design D§4.1-4.2: calibration showed Rx is ~90% of the energy (CONFIRM E13), which makes both "must".
- Effect on results: only when the flags are set (default off).
- Spec/design deviation:
  1. **N-A** is implemented as an energy charge only: the whole frame still occupies the channel and can collide; a frame that arrived corrupted is charged in full; beacons, ACKs, elections, NACKs and handovers are always charged in full; DATA addressed to the node is charged in full. This is a modelling assumption (the modem must be able to decode the header separately, D§9 risk).
  2. **N-C** is simplified: heads and sinks beacon at an interval that doubles (to at most `TrickleMaxFactor` = 2 x the base period; the design allows 2-4) whenever the quantised state (own role/cell/Hmin and the set of heads/sinks with their cells and Hmin) has not changed since the last beacon, and resets to the base period otherwise. Members beacon at start-up, after a cell change and 8 s before each election round. Every beacon advertises its next interval; a neighbour row expires after max(3 base periods, 3 x the advertised interval). The factor 2 keeps a silent head detectable within ~300 s; 4 would need the quorum-based detection of D§3 P4, which is not built.
  3. **N-B (members sleep) is not implemented.** Members still listen all the time.
- Result (N = 250, 3000 s, warm-up 500 s, 0.004 pkt/s, published E0 = 300 J, RoundRobin; `results/reference/d5-smoke/energy_flags_N250_rate0.004_E300.csv`):
  | flags | PDR | Rx share | first node dead | half dead | beacon B / delivered |
  |---|---|---|---|---|---|
  | none | 0.162 | 0.892 | 1036 s | 1789 s | 847 |
  | N-A | 0.178 | 0.878 | 1107 s | 2038 s | 777 |
  | N-C | 0.164 | 0.882 | 1542 s | 2948 s | 318 |
  | N-A + N-C | 0.161 | 0.861 | 1795 s | none | 331 |
  N-C cuts beacon bytes by 62% and moves the half-dead time from 30 to 49 minutes; N-A helps little at this light data load (overheard data is a small part of Rx; it should matter more at higher load). Rx is still 86% of the energy and the first node still dies within 30 minutes, so the published E0 is not usable for long runs (CONFIRM E13 stays open).

## 2026-11-06 02:00 — D7a head failure: suspect tier, planned handover, unplanned takeover, failure injection
- What: `ErgrHandoverHeader` (115 B) and HANDOVER frames; selector hooks `OnRoleChange`, `OnHandoverOut/In` with the CB implementation; suspect tier in routing (`NoteForwarderTimeout/Alive`, `IsSuspect`); `ErgrEmhcRouting::Kill()`; `KillHeadsAtS`/`KillHeadsCells` in the scenario; `fwd_cell` in decisions.csv; events `HEAD_SUSPECT`, `HEAD_DEAD`, `HANDOVER_OUT/IN`, `NODE_KILLED`; energy-model bookkeeping records a death when the battery is set to 0 (patch P4). 3 new tests (42 cases pass), including spec test 14.
- Why: kickoff §2.1 (failure detection), §2.3 (planned vs unplanned handover), §4 D7, design D§3 P3-P5, P8.
- Effect on results: all feedback methods now skip a suspect forwarder; ranking unchanged (seeds 101-103: RR+FB 0.501, FW 0.560, CB 0.521, O 0.594).
- Spec/design deviation:
  1. **Suspect = 2 consecutive ACK timeouts** to a forwarder with no ACK or beacon from it in between; it is skipped for one beacon period, **but used again if nothing else is eligible**. The first version excluded the only candidate and broke test 12 (a relay with a single next hop stopped forwarding under 30% loss); caught by that test.
  2. **"Dead" is the existing 3-beacon expiry of a head row** (single observer, event `HEAD_DEAD`); the quorum of 2 from D§3 P4 is not implemented (it would need extra messages). Consequence: detection of a silent head takes up to 150 s; the suspect tier covers the fast reroute (a few seconds). Recorded as CONFIRM E23.
  3. **Planned handover** is sent whenever a live head stops being head (left the cell, outranked, conflict). The successor is the best-scored non-head member of the cell in the old head's table; it applies the model if it becomes head within `HandoverKeepS` = 400 s, otherwise the model is dropped. A is sent as 9 uint16 diagonal + 36 int16 off-diagonal values at 1/16 (spec §6.4 says "45 x uint16"; off-diagonals can be negative, so they are int16).
  3b. **Unplanned takeover** (a head is elected without a handover): the node adopts the strongest model the cell shared on beacons, with A rebuilt as lambda0 I + diag(evidence) and scaled by 0.7. Inflation of the survivors (BetaInflate) happens separately on a lost head, as K§2.3 says.
  4. **Failure injection** kills one random head in each of `KillHeadsCells` random cells that have at least two live nodes and a head, by emptying the battery (it appears as `NODE_KILLED` then `NODE_DIED`). Recovery-time analysis from the logs is not written yet (needs `analyse.py`).

## 2026-11-05 22:00 — D6b oracles (O, GO), regret, offline-fitted A1
- What: new `oracle-evaluator.{h,cc}` (the only reader of true positions/shadowing), `selector-oracle.{h,cc}` (`Oracle`, and `GeoOracle` = GeoOnly), `SelectorFittedLogistic` (A1) next to `SelectorFixedWeight` sharing `SelectByWeights`, `scripts/fit_a1.py`, `configs/a1-weights.txt`; a decision observer in routing, `OnRegret` in the metrics and four summary columns (`decisions_scored`, `regret_mean`, `regret_mean_multi_viable`, `share_multi_viable`); RoundRobin now logs the features of its chosen option (for the fit; the choice does not use them). 3 new tests (39 cases pass).
- Why: kickoff §2.4 / §4 D6, spec §11 O, GO, A1 and §13.1 regret.
- Effect on results: none on existing methods.
- Spec/design deviation:
  1. **Regret definition.** U(j, p) = trueSuccess(j, p) x progressWeight - lambdaE x Etx(p), with the energy price the evaluated method itself used; regret = best U over all candidates minus U of the chosen option, floored at 0, with the true shadowing at decision time and collisions ignored. Options are limited to the full power level unless `RegretAllPowerLevels=true` (the first version let the oracle use cheaper power levels that CB could not choose, which inflated CB's regret to 1.7 - fixed). `multi_viable` = at least two candidates with predicted margin > 0.
  2. **O/GO use ACK requests and retries like every other method** (same feedback path), so their PDR is below a perfect-knowledge bound for the whole system; the regret column is the clean per-decision measure.
  3. **A1 is fitted on round-robin-with-feedback logs of training seeds 101-107** (27401 head decisions) with the learner's own prior (lambda0 = 1), and evaluated on held-out training seeds 108-110. `fit_a1.py` refuses file names that look like test seeds and refuses logs without features (the first fit returned all zeros because RR did not log features: caught by reading the result, fixed in both places).
- Result (held-out training seeds 108-110, N = 100, 2500 s, 0.004 pkt/s, E0 = 10000 J; `results/reference/d5-smoke/compare_a1_heldout_seeds108-110.csv`):
  | method | PDR | regret |
  |---|---|---|
  | RR + feedback | 0.475 | 0.190 |
  | CB | 0.534 | 0.146 |
  | A1 (fitted offline) | 0.563 | 0.081 |
  | FW (hand-set) | 0.584 | 0.069 |
  | O (oracle) | 0.641 | 0.000 |
  On the seeds 101-103 run: GO 0.527 (regret 0.10), O 0.626. Ordering by regret matches ordering by PDR for all methods.
- Reading: static rules on these features (hand-set or fitted) beat CB over 2500 s; the learner has not accumulated enough evidence per head. The gap to the oracle (0.64) shows how much the true shadowing is worth beyond the features (the measured beacon SNR only partly captures it).

## 2026-11-05 18:00 — D6a baselines: FixedWeight, QLearning, CarpEwma, LfaQ; shared feature builder
- What: new `cb-features.{h,cc}` (the 9 features, moved out of `SelectorCbV5` unchanged), `selector-fixed-weight`, `selector-q-learning`, `selector-carp-ewma`, `selector-lfa-q`; optional 2-byte value on beacons (`hasQ`); `Decision::aux`, `NeighbourEntry::advertisedQ`; 4 tests (36 cases pass); more variants in `scripts/compare_smoke.py`.
- Why: kickoff §2.4 and §4 D6. All use the same eligibility, feedback manager and overhead accounting.
- Effect on results: new rows in the comparison; CB itself is unchanged (refactor checked by the existing tests).
- Spec/design deviation:
  1. **FW weights are hand-set** (`params::selector::kFwWeights`), chosen from the physics (long distance bad, high SNR margin very good, shallower and closer to the sink good, long queue bad). They were set before looking at any result and never tuned, but they are not independent of what the author knows about the channel. The supervisor should decide a policy for them (hand-set vs the offline fit of A1).
  2. **QL and LinQ advertise their value on every beacon** (+2 B each, counted in `ctrl_bytes_beacon`); QL uses Q = 1 + 0.1 (myHmin - hmin_j) for untried forwarders; sinks are terminal (value 0). All CONFIRM C1.
  3. **CARP uses bitmap feedback** (no ACK on first attempts; retries ask), which is why its delay is very large (labels and retries wait for the next beacon or the 150 s expiry).
- Result (training seeds 101-103, N = 100, 2500 s, 1 m/s, E0 = 10000 J, 0.004 pkt/s; means; `results/reference/d5-smoke/compare_all_rate0.004.csv`):
  | method | PDR | attempts | delay (s) | energy/delivered (J) |
  |---|---|---|---|---|
  | RR (published) | 0.288 | 1327 | 3.0 | 109 |
  | RR + feedback | 0.483 | 4092 | 7.7 | 105 |
  | QL | 0.435 | 4200 | 7.9 | 113 |
  | LinQ | 0.462 | 3892 | 6.9 | 102 |
  | CARP | 0.517 | 3936 | 117.8 | 99 |
  | CB | 0.520 | 3693 | 6.6 | 90 |
  | **FW (no learning)** | **0.552** | 3227 | 5.5 | 82 |
- **Honest reading:** a fixed, physically sensible weight vector on the CB features is as good as or better than CB over 2000 s, and about equal over 12500 s (CB 0.532, FW 0.527, RR+FB 0.485; 2 seeds). Most of CB's gain over RR+FB therefore comes from the features (above all the measured link margin), not from learning the weights online. This does not yet support claim N1 as "learning beats a good static rule". What would: a cold FW that is wrong (e.g. different channel), non-stationary links, larger N and longer runs, or the offline-fitted A1 as the principled static baseline. Not tuned; reported as is. See CONFIRM E22.

## 2026-11-05 14:00 — D5 SelectorCbV5: Thompson-sampling forwarder selection, ACK gate, energy price, sharing
- What: new `selector-cb-v5.{h,cc}`; the `SelectorServices` interface and beacon hooks on `ForwarderSelector`; `CbErgrPropagation::MeasuredBeaconSnrDb`; `ErgrEmhcRouting::SetLinkModel/SetSnrMeter`; additions to `cb-learner-model.h` (`SetTheta`, `LogDetOwn`, `IsPeerAccepted`); `scripts/compare_smoke.py`; 5 new tests (feature builder = spec test 6, score, ACK gate, learning, sharing; 32 cases pass).
- Why: kickoff §4 D5 and spec §8. Power is off by default, lifetime price on, sharing on, AckMode Gated, `MarginFromRssi` on (K§2.3).
- Effect on results: first CB-vs-RR numbers (below). No tuning.
- Spec/design deviation:
  1. **`NodeLost` now fires only when the lost row was a head.** Reason: K§2.3 allows covariance inflation only on a death or unplanned head loss; a table timeout of an ordinary member is neither. A head that merely moved out of range is still counted (the proxy until D6's suspect/dead detection).
  2. **`MarginFromRssi` uses the SNR measured on the candidate's last decodable beacon** (the receiver records it from the channel's link decision) plus the power-level offset, minus the SNR needed for PER 0.1. This is receiver-side information (what a modem measures); the learner never sees the true shadowing or any future value.
  3. **The sharing blob carries diag(A_own - lambda0 I) and thetaHat** (quantised), and a received blob is used only if the sender is in the same cell and passes the maturity/cosine gate; the stored evidence of each source ages with the same forgetting (K§2.3 incremental sharing).
  4. `chosen_rank` in decisions.csv is the rank of the chosen option by its **mean** predicted success (1 = best), so it shows how often Thompson sampling explores; it is not the rank by score.
  5. Optional 10th feature (`OnwardFeature`) is not built; D = 9 only.
- Smoke result (training seeds 101-103, N = 100, 2500 s, warm-up 500 s, 1 m/s, E0 = 10000 J, mean over seeds; `results/reference/d5-smoke/`):
  | rate | method | PDR | attempts | delay (s) | energy/delivered (J) |
  |---|---|---|---|---|---|
  | 0.004 | RR (published) | 0.288 | 1327 | 3.0 | 109 |
  | 0.004 | RR + feedback | 0.483 | 4092 | 7.7 | 105 |
  | 0.004 | CB | 0.520 | 3693 | 6.6 | 90 |
  | 0.02 | RR | 0.153 | 5798 | 2.5 | 76 |
  | 0.02 | RR + feedback | 0.167 | 16983 | 6.3 | 115 |
  | 0.02 | CB | 0.202 | 16345 | 5.5 | 94 |
  CB beats RR and RR+FB on every seed at both loads. Most of the gain over published RR comes from the feedback and retransmission (RR+FB), CB adds 3-4 PDR points on top and uses ~10-20% less energy per delivered packet than RR+FB.
- Things that did NOT show an effect (reported, not tuned): the ACK gate (`AckMode=Gated` vs `Every` give identical results, AR rate 1.0: the model width stays above TauW = 0.3 because each head sees too few decisions in 2000 s) and sharing (`EnableSharing=false` gives PDR 0.205 vs 0.202 at 0.02 pkt/s). So claim N2 and N3 are not supported by these short runs; longer runs (20000 s, N = 250) and the N-A/N-C energy flags are needed before judging them.

## 2026-11-05 08:00 — D4 feedback: ACKs, receipt bitmaps, retries, ground truth, links/decisions CSVs
- What: new `feedback-manager.{h,cc}`, `held-packet.h`; routing now sends DATA with random-start hop sequence numbers, ACK timers, ACK frames, receipts in beacons and retries; `MetricsCollector` gained attempt/label/ground-truth hooks, `links.csv`, `decisions.csv` (with `hop_seq`), label statistics in `summary.csv`, new columns in `nodes.csv`, and the attempt consistency check (tracked = labels + pending + discarded). RoundRobin got `WithFeedback` and `AckRequest` attributes; scenario got `SelectorAttributes`. Test hooks: `ForceRole`, `InjectPacket`, `AutoElection`, `CbErgrPropagation::SetFrameFilter` and `ExtraLossProbability`. Tests 12 and 13 added (27 cases pass).
- Why: kickoff §4 D4 and spec §9. Gives every learner the same delayed, censored 0/1 labels, with no label ever coming from silence.
- Effect on results: RoundRobin without feedback is unchanged except for random-number order and patch P5 (see below). New: RR with feedback ("RR+FB").
- Spec/design deviation:
  1. **ACK timeout is longer than the spec formula** (CONFIRM E17). Aqua-Sim hands a frame to the MAC two airtimes after it starts arriving (collision check one airtime, signal cache another). With the spec formula every ACK arrived after its timer fired (all labels were timeouts, 32% false negatives). The timeout is now (1 + PhyRxDelayAirtimes) x (airtime(DATA) + airtime(ACK)) + 2d/1500 + TProc + TGuard, with the extra factor measured as 1. Alternative not taken: patching the PHY to deliver after one airtime (it would change the collision window of all frames).
  2. **Test 13 follows K§2.3, not spec §14**: with "ACK timeout = label 0 at once", the lost-ACK attempt is labelled 0 (source ACK_TIMEOUT, ground truth received = false negative); the retry is deduplicated at the relay, which ACKs it again. The spec's "labelled 1 later from the bitmap" cannot happen because the first label wins.
  3. **Member hand-offs use ACK + retry in feedback mode** but their labels are logged, not learned (K§2.1: only heads learn). Without this, every method loses 30-50% of packets on the first (member to head) hop.
  4. **A void NACK is a failure label** only if the attempt has no label yet; if the ACK already arrived the label stays 1 (first label wins) and the NACK path re-routes the packet.
  5. **`duplicate_labels`** counts ACKs that arrive for an attempt that already has a label (late ACKs), not repeated bitmap reads.
  6. **RR+FB is an extra baseline** (not in K§2.4): the same round-robin choice with ACK requests and retries. It separates the gain of retransmission from the gain of learning.
  7. Ground truth is kept inside `MetricsCollector` (logging only); `ForwarderSelector` has no access to it.
- Bugs found on the way: ACK timeout too short (item 1, caught by the label statistics); decisions column order shifted after adding `hop_seq` (tests read by column name so they did not break).
- Baseline shift: RR without feedback now gives PDR 0.273 on training seeds 101-103 (was 0.294 before D4). Cause: patch P5 (half-duplex) was switched on after the earlier runs, and the random hop-sequence start changes the random-number order. Within seed noise; not tuned.

## 2026-11-05 03:00 — D3 ERGR-EMHC-g + RoundRobin pipeline (routing, election, metrics, scenario)
- What: new `grid-geometry`, `ergr-headers`, `neighbour-table`, `recent-set`, `forwarder-selector`, `selector-round-robin`, `localisation-noise`, `current-drift-mobility`, `metrics-collector`, `ergr-emhc-routing`, `scenario-config` (model/); `scenario-runner` (helper/); `examples/cb-ergr-scenario.cc`; `configs/{base,p0-published,smoke,e0-10000}.txt`; patch P5 (half-duplex). 9 new test cases (headers, grid, table, duplicate filter, small run, determinism).
- Why: kickoff §4 D3. The base scenario now runs end to end and writes summary/nodes/packets/events CSVs (frames.csv with `--LogFrames=true`) with reason codes and the consistency checks of K§2.6.
- Effect on results: first RoundRobin numbers (see PROGRESS.md calibration). No tuning was done.
- Spec/design deviation: the following choices fill gaps in the docs; each is also in CONFIRM.md (E10-E15):
  1. **Sinks are candidates from anywhere within range**, not only from the 26 adjacent cells. Reason: a head in the same cell as a sink would otherwise have no way to reach it.
  2. **Strict progress** (K§2.1 "loop guard") is implemented as: candidate has a smaller Hmin than mine OR is shallower than me (sinks always pass). The spec's `Hmin_j <= Hpkt` rule is applied as well. The previous hop is always excluded.
  3. **Members** hand their packet to a head of their own cell (round robin over the heads); the head restarts the hop budget from its own Hmin (Hpkt = Hmin of the head). The member to head hop does not use a hop.
  4. **Hmin** is recomputed from beacons (50 s old at worst) instead of the ordered start-up wave of D§3 P6. Convergence takes a few beacon periods; start-up metrics are not reported yet.
  5. **Election**: first round at 55 s (+2 s per cell id) with beta = 0, then every 400 s. A node ranked <= k claims in slot (rank-1) s + jitter. Extra triggers (debounced to one per beacon period): a head of my cell disappears or leaves, I join a cell with fewer than k heads. A node that hears k better heads in its cell retires (RETIRE frame). Claims are unconditional; conflicts are resolved by that retire rule.
  6. **RoundRobin** takes the next larger id after the one used last (wrapping); the list is the current eligible set.
  7. **Traffic** is generated inside the routing class (Poisson per node), not with PacketSocket applications.
  8. **Packet fate**: round robin cannot see losses, so a packet with no activity for 120 s (`packetTtlS`) is classed `TTL_EXPIRED` (a ground-truth loss); younger unresolved packets are "in flight" and excluded from the PDR denominator.
  9. **Collisions** are counted as PHY error events minus known PER losses, per node, not per frame type (the PHY does not say why it dropped a frame).
  10. **Mobility field** is a stand-in (CONFIRM E10) because the pilot was not supplied.
- Bugs found and fixed on the way: beacon base size was 24 B, really 25 B (caught by the ns-3 buffer assertion; now covered by the header test); the runner refactor dropped `SetSeed/SetRun` so seeds were ignored (caught by the determinism test); the `params` column of summary.csv contained the output path, breaking byte-identical output (now excluded).

## 2026-11-04 23:00 — D2 PHY/channel: SNR-based reception, shadowing, energy fixes
- What: new `acoustic-link-model.{h,cc}`, `gm-shadowing.{h,cc}`, `cb-ergr-propagation.{h,cc}`, `helper/cb-ergr-helper.{h,cc}`, `examples/link-per-curve.cc`, `scripts/plot_per_curve.py`; upstream patches P1-P4 (see PATCHES.md); 3 new tests (airtime, link budget, shadowing).
- Why: spec §4.1, §4.3-§4.5, kickoff §4 D2. Upstream reception ignores distance and power (CONFIRM D2), so a subclass of the propagation model decides each frame by PER.
- Effect on results: all runs now use SNR-based loss and the corrected Tx/Rx energy; nothing else yet.
- Spec/design deviation: three choices to defend in the thesis:
  1. **Shadowing is a pure function of (link, time)**, computed from counter-based noise on a 5 s grid, with the run seed drawn once from an ns-3 stream (spec says "update on access"). Reason: the oracle and regret logging read the true shadowing; with a shared stream they would change the random path of the run. Statistically identical (tested: std, mean, correlation e^-1 at one tau).
  2. **Source level maps from Pt in dB**: SL = SL_full + 10 log10(Pt/Pt_full). Scales 0.25/0.5 then give -6.02/-3.01 dB, matching the spec's -6/-3 dB offsets, so the later power patch only needs to scale Pt.
  3. **Frames lost to noise still occupy the channel**: the propagation sets pR = 0 and the PHY (RXThresh 0.5) treats them as "below threshold": they can still collide with other frames and cost Rx energy, but are not delivered.
- Bug found: the first link test received nothing. Cause: counting at a PacketSocket never fired in Aqua-Sim; the frames did reach the MAC. The example now counts at the PHY `Rx` trace instead. Upstream socket delivery was not investigated further (not needed).
- Bug found: upstream charges Rx energy for the propagation delay, not the airtime (patch P2), and overwrites the energy model's TxPower with the acoustic Pt (patch P3). Both change energy by a large factor relative to the spec's assumptions.

## 2026-11-04 21:00 — D1 learner core (pure C++, tests 1-5, 7, 8 + new)
- What: new `model/small-matrix.h`, `serial-number.h`, `receipt-window.h`, `energy-price.h`, `cb-learner-model.h`; 14 test cases in `test/cb-ergr-test-suite.cc`.
- Why: kickoff §4 D1. All maths is header-only with no ns-3 dependency, so it can be unit-tested and read in isolation. `D` is a template parameter (9, or 10 with the onward feature).
- Effect on results: none yet (no selector uses it).
- Spec/design deviation: yes, three, all from kickoff §2.3 / D§3 P8:
  1. Forgetting runs at **decision time only** (`BeginDecision`); `UpdateWithLabel` and `AddSendEvidence` never move the clock.
  2. Sharing is **incremental**: effective A = A_own + sum of aged per-source diagonal evidence; a new message from a source replaces its old contribution. Only the diagonal is shared on beacons (spec §6.3 blob has diag(A) only).
  3. `ImportHandover(planned=false)` scales A by BetaHandover; planned keeps it; `InflateAfterLoss` is separate and is meant to be called only on death / unplanned loss.
- Test note: the "within 0.05 of sklearn" check (spec test 2) compares exact-Hessian Newton steps to a plain gradient-descent optimum of the same L2-regularised loss (lambda = lambda0 = 1). sklearn is not installed here and is not needed; same optimum, no extra dependency.
- Bug found while testing: first version of the forgetting test assumed 60 s had elapsed but 65 s had (test error, not code error). Fixed in the test.

## 2026-11-04 20:45 — D0 restructure to the kickoff layout
- What: added `model/cb-ergr-params.h` (single home of all defaults), `configs/`, `docs/` (spec, kickoff, CODE-GUIDE copies), `doc/CODE-GUIDE.md`, `doc/CHANGELOG.md`, `PROGRESS.md`. The design doc v5.2 was supplied only as chat text; save it as `docs/cb-ergr-design-final-detailed.md` (not yet in the repo).
- Why: kickoff §0 rules 8-11 and §4 D0.
- Effect on results: none.
- Spec/design deviation: yes. Cell side default is now 750 m, k = 2 heads, `EnablePower` false, `MarginFromRssi` true (all kickoff §2). This resolves open question CONFIRM A1 for ERGR-EMHC-g; P0 keeps its own value.
