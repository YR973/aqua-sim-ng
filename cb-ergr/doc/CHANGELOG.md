# CHANGELOG (newest first)

One entry per change. Format fixed by the kickoff (rule 8).

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
