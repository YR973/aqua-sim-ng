# CHANGELOG (newest first)

One entry per change. Format fixed by the kickoff (rule 8).

## 2026-11-07 16:00 — Step 2: counterfactual log (`LogCounterfactual`); tooling for Steps 3 and 4
- What:
  * **`LogCounterfactual` (default off)** writes `counterfactual.csv`, simulator side only (no node reads it). `ErgrEmhcRouting::RoutePacket` passes the ids of the eligible candidates (and member hand-offs the heads of the own cell, marked shadow = -2) to `LossBudget`, which attaches them to the DATA attempt, retries included (every transmission is its own row). When the attempt is classified, each candidate gets a status from ground truth for the SAME frame: R received, P below the PER draw, C collision (DATA / beacon-Hello / ACK-NACK overlap at that candidate), H the candidate was transmitting, D dead, O out of range. The chosen candidate's status is the real outcome. `WhyLostAt` is now the one function that explains a loss (used for the real hop and for every candidate), so the per-hop budget and the counterfactual cannot disagree. A shadow `FixedWeight` selector (physics weights, `ShadowSelectorAttributes`) is asked the same question at every decision (`shadow` column; it is never used or trained). Test `NoiseBurstAndCounterfactualLogAreConsistent`.
  * `scripts/counterfactual.py` (20 seeds x 4 cells x FW and CB-cold with `AckMode=Every`), result in `results/reference/step2/counterfactual.md`.
  * **Noise burst** for the change scenario: `NoiseBandStartS` / `NoiseBandEndS` (defaults 0 / -1 = always on, in `params::channel`); the oracle evaluator sees the same burst.
  * **Instrumentation for Step 4** (all reporting only, off or free by default): `MeasureCpu` (std::chrono around `Select`, `OnSent`, `OnLabel`), `cost.csv` per run (CPU mean/p95/max in us, beacon bytes split into fixed part / receipt bitmaps / model blob / Q value, neighbour-table rows, peak pending attempts, `sizeof` of the main records, CB diagnostics: Cholesky jitters and resets, non-finite decisions or parameters, largest |theta| and trace(A)), `resources.csv` (wall time, peak RSS from `getrusage`), `regret_by_time.csv` (100 s bins: regret against the per-hop oracle, regret against FW at the same decision, mean gap between the two best arms, share of decisions with two truly viable arms (hop success >= 0.5) and with a gap < 0.05), `ForwarderSelector::Diagnostics`. `scripts/perf.py` (Step 3 tables, windowed PDR and recovery times), `scripts/statlib.py` (t distribution, paired CI, TOST, Holm; no scipy here), `scripts/opcount.py` (analytic operation counts, MCU time estimates, learner memory).
- Why: user request of 7 Nov 2026, Steps 2 to 4.
- Result of Step 2 (hop ACK + retries, Csma, calibrated channel, 20 seeds; full tables in `results/reference/step2/counterfactual.md`):
  1. **What happens to the frame sent to the chosen forwarder** (share of routed DATA transmissions). FW, N = 100, 10 sources: received 0.654, below the PER draw 0.194, collision with DATA 0.080, with beacon/Hello 0.026, with ACK/NACK 0.026, half-duplex 0.008, out of range 0.007. CB-cold: 0.605 / 0.253 / 0.076 / 0.024 / 0.025 / 0.004 / 0.010. At 30 sources DATA collisions grow to 0.21-0.23 and received falls to 0.46-0.49; at N = 300 the beacon share doubles (0.037-0.046). So with hop ACKs the first loss is the link (PER 16-28% of transmissions), then DATA-DATA collisions; and **CB loses more than FW to PER (0.21-0.28 against 0.16-0.21): it picks weaker links more often**.
  2. **Time-split per-candidate oracle** (success per (sender, candidate) including collisions from the first half of the window, applied to the second half): hop success 0.483-0.611 against the chosen forwarder's 0.505-0.643; oracle - chosen = -0.043 / -0.009 / -0.052 / -0.036 for FW and +0.016 / -0.014 / +0.001 / -0.007 for CB (95% CI about +-0.01). **The history-based ceiling is not higher than the static rule's own hop success**: the per-pair success including collisions is mostly noise (only 27-56% of second-half decisions have >= 2 first-half samples for every candidate, and collision outcomes do not repeat). The only upper bound that exceeds FW is the hindsight bound (some candidate would have received the frame: 0.65-0.79), which uses the outcome of each transmission and is not reachable by any learner. CB's positive numbers at 10 sources are a measure of CB's own shortfall against FW, not of room above FW.
  3. **CB against FW at the same decisions**: CB's choice differs from the FW shadow's in 32-37% of the decisions; on those decisions CB's choice is received in 0.51-0.58 of the transmissions and FW's would have been received in 0.59-0.73 (10 sources), so CB's deviations cost 0.08-0.16 hop success there; at 30 sources N = 100 CB's deviations are better (0.544 against 0.513: FW sometimes picks a dying relay, 5% of FW's transmissions at N = 100 with 30 sources go to a dead receiver, 4% for CB). CB's deviations lose mainly to PER (0.23-0.27 of those transmissions), not to collisions (0.08-0.11).
- Spec/design deviation: none (reporting only).

## 2026-11-07 10:00 — Step 1 of the 7 Nov request: label bugs, QL target, ACK gate, Aloha binding, overhearing audit, oracle+power cause
- Context: the user's 5 Nov analysis of the cloud copy of commit bb651c6 listed bugs in labels and baselines. "Phase 1" (g beacon load fix, ACK-gate re-run, oracle lambda variants, power-lifetime test, headroom re-run) was not part of this session's instructions; what exists here is the 6 Nov work (E34, loss budget, TPre 0, CSMA ACK timeout, 20-seed headroom), committed at bb651c6. Not done from that list and said so in the report: oracle lambda variants, power-lifetime test, a separate beacon-load fix (g's beacons are 15% of the packets lost at 1 source, CHANGELOG 6 Nov 14:00).
- 1a/1b **Late evidence and censored labels** (`FeedbackManager`, `PendingAttempt::timedOut`, `LabelSource::Censored`). What: an ACK timeout still starts the retry at once and still feeds the suspect tier, but it no longer erases the attempt or labels it 0. The record is kept until a late ACK or a receipt bitmap arrives (label 1, or the explicit 0 of a bitmap), or until expiry (3 beacon periods = 150 s); an attempt with no evidence at expiry is CENSORED: nothing is shown to the learner, `decisions.csv` has label -1 / source CENSORED, the summary counts it (`censored_labels`, `censored_received` = how many had in fact arrived). The same holds for an attempt sent without an ACK request whose receipt never comes (it was label 0 at expiry before). Attributes (defaults in `params::feedback`): `KeepRecordAfterTimeout` (true; false = old rule), `CensoredLabels` = Drop (default) | Zero (old rule). New summary columns `ack_timeouts_fired`, `ack_timeouts_false` (the forwarder had the frame), `peak_pending_attempts`, `label_censored_frac`. A late bitmap after the timeout does not start a second retry.
  Why: at an ACK timeout the learner was told 0 although the frame had often arrived (false timeouts) and a late ACK or bitmap could never correct it (`Resolve()` erased the record).
  Effect (RoundRobin with hop feedback, N = 100, 10 sources, seed 101, g-base): Csma 2410 ACK timeouts fired, 366 of them false (15%); 1514 attempts censored, 123 of them (8%) had in fact arrived; the false-negative rate among the 0-labels that are left is 0.0 (Aloha 0.002), all coming from explicit bitmap zeros. Tests: `AckTimeoutKeepsTheRecordAndExpiryCensors` (late ACK -> 1, no evidence -> not shown to the learner and counted, CensoredLabels=Zero and KeepRecordAfterTimeout=false reproduce the old rule, non-AR attempt censored and retried), test 12 and test 13 updated (the late receipt bitmap of the relay corrects the lost-ACK attempt to 1; old rule still selectable).
- 1c **QL and LinQ target** (`SelectorQLearning`, `SelectorLfaQ`). What: reward 1 on reaching a sink and 0 for every other hop, V_sink = 0 terminal; a successful hop to j has target gamma V_j (1 at a sink), a failed hop leaves the packet at i, so its target is gamma V_i (V_i = the node's best Q), not gamma V_j. Initial Q = gamma^hmin_j (tabular QL). The old rule (target (y - 0.1) + gamma V_j, initial Q 1 + 0.1 dHmin) stays as `Legacy=true` ("QlLegacy", also for LinQ). Why: the old target grew with path length and credited a failed hop with V_j. Tests `QLearningTargetIsReachSinkProbability`, `LinearQNewTargetBootstrapsFailuresFromTheOwnValue`, old tests moved to Legacy.
- 1e **Loss-budget binding under Aloha**. What: the channel now reads (source, seq, forwarder) from the DATA header and the loss budget binds each air transmission to the oldest unbound attempt with that identity; the per-sender FIFO is gone. Why: under Aloha a frame handed to the MAC may never go on the air (the BroadcastMac drops it while the modem sends or receives: "never_on_air" 1% of attempts), which shifted the FIFO and made every later attempt of that sender bind to the wrong frame (hop-level "received" 0.21 against 0.52 ground truth). Effect (N = 100, 10 sources, RR+FB, seed 101): Aloha hop delivery 2966/4745 = 0.625 against ground truth 0.627; Csma 3097/5016 = 0.617 against 0.617. Test `LossBudgetHopCountMatchesTruthUnderAloha` (both MACs within 0.08 on a 60-node run). The loss-budget numbers of the 6 Nov entries are Csma and were not affected.
- 1f **`AckGate=ThompsonDisagree`** (`SelectorCbV5`, `DisagreeDraws` M = 4, `params::learner::kAckDisagreeDraws`). What: for a first attempt in `AckMode=Gated`, draw M extra posterior samples (same posterior, fresh normals), recompute the score of every (candidate, level) option and request an ACK only if some sample's argmax differs from the chosen option; priority packets, the 10% audit and every retry still ask. The Logit and ProbStd gates are unchanged. FW has no posterior: in comparisons FW keeps `AckRequest=true` (an ACK on every attempt) and CB runs with `AckMode=Every` as the like-for-like row; the gated rows only show the ACK saving. Test `ThompsonDisagreeGateAsksOnlyWhenTheChoiceIsUncertain`.
- 1g **Frames not addressed to the node.** Audit of `ErgrEmhcRouting::Recv` and everything it calls: DATA (`forwarderId != me`), ACK (`senderId != me`), HANDOVER (`toId != me`) are only counted by the metrics (`framesOverheard`); a NACK for another node returns at the first line of `OnNack`. The routing layer never reads an SNR, RSSI or neighbour entry from them (SNR comes from beacons only, via the helper's `m_beaconSnr`). The frames a node does use although they are not unicast to it are all broadcast control frames, which the rules allow: beacons (neighbour table, receipts, model blobs, Q values) and the election-kind frames (Hello, Retire, Exit, DeadReport, Solicit: head flags and counts of the own cell). Nothing to remove. Test `FramesForOtherNodesChangeNothing` injects a foreign DATA, ACK, NACK and HANDOVER into the running line network and checks that the neighbour table, role, pending attempts, labels and deliveries do not change. The PHY of course still charges receive energy for overheard frames and an overheard frame still locks the receiver (that is the channel, not the protocol).
- 1d **Why the oracle with power levels gains PDR** (diagnosis only). Setting: g-base, N = 100, Csma, 30 sources, seeds 101-110, calibrated channel. Oracle -> Oracle+power: PDR 0.646 -> 0.685 with the paper's 300 J; **with 100,000 J (no node can die) 0.653 -> 0.686**: the gain does not come from node deaths (first death 830-970 s -> 910-1200 s with power, but the gain is the same without any death). Hop success 0.479 -> 0.504, attempts per run -7%. Loss budget (seeds 101-106, 100,000 J): DATA-DATA collisions 0.325 -> 0.293 of all hop attempts (-3.2 points), PER loss 0.098 -> 0.107 (+0.9). Of the DATA collisions, those where some overlapping DATA frame was DECODABLE at the victim receiver fall from 0.276 to 0.206 (-7.0 points); those where an overlapping frame STARTED INSIDE the victim's window stay at 0.20. Mechanism, from `AquaSimPhyCmn::PrevalidateIncomingPkt`: a frame that is decodable at a node puts the node in RECV for its airtime; a second frame arriving meanwhile is lost; a frame below the threshold (undecodable) does not lock the node and only sets the collision flag, which the next decodable arrival resets. So it is not true that lowering the power cannot reduce interference here: it cannot reduce the number of copies (every node within R gets one), but it turns decodable overheard copies at the far neighbours into undecodable ones, and those no longer lock the receiver, which removes the collisions in which the interferer started BEFORE the victim frame. Not changed (PHY review, E38); reported. New loss-budget sub-counters document it (`of which: some overlapping DATA frame was decodable ...`, `... started inside the victim's window`).
- Spec/design deviation: the label policy replaces K§2.3 "label 0 at once" for ACK timeouts (user request); the old rule stays selectable.

## 2026-11-06 20:00 — Step 4 fix 2 (CSMA and the ACK timeout) and Step 5: headroom with 20 seeds, PDR and energy, oracle with power levels
- What:
  * **Fix 2: the hop-ACK timeout now includes the MAC backoff** (`MacDelayAllowanceS` = (CwMin + CwControl) x slot = 1.0 s for `Mac=Csma`, 0 for Aloha). Found in the first 20-seed headroom run: with Csma every method with feedback looked WORSE than with Aloha (RR+FB at N = 300, 10 sources: 91% of ACK labels were timeouts, 33% of them false, 9.4 retransmissions per delivered packet, hop success 0.385, 2x the energy per packet) because the DATA and the ACK now wait in the CSMA backoff but the timeout of the spec formula (airtime + propagation + TProc + TGuard) did not allow for it. Effect on test 16: none (P0 has no ACK timeouts). Effect on g: RR+FB, N = 300, 10 sources, Csma: PDR 0.716 -> 0.736, energy per delivered packet 86.9 J -> 47.2 J (table below; the row "before" is `results/reference/headroom2/headroom_before_ackfix.md`).
  * `SelectorOracle` takes `EnablePower`, `LambdaE` (fixed energy price, the spec's initial 0.25 per J) and `TxPowerFullW`: it then maximises success x progress - LambdaE x Etx(level) over candidate AND level. The scenario passes the radio's `TxPowerFullW` to CB and the oracle (CB used the params default of 2 W before, wrong for the 8 W paper radio; its energy term only matters with `EnablePower=true`).
  * `scripts/headroom.py`: 20 seeds (101-120), PDR and energy per delivered packet, paired 95% CI of the gaps, an "Oracle+power" column; test for the oracle with power levels (49 cases pass).
- Why: user request of 6 Nov 2026, steps 4 and 5; from now on every compared number uses 20 seeds.
- Effect on results: all ERGR-EMHC-g results with `Mac=Csma` and feedback produced before this entry are superseded.
- Setup of step 5: `configs/g-base.txt` (paper radio, TPre = 0, lean beacons, solicit, equal-Hmin fallback), calibrated channel (shadowing sigma 5 dB), 0.07 pkt/s per source, 2000 s, node speed 1 m/s (as in g-base), seeds 101-120. "FW-tuned" = `configs/fw-tuned-v2.txt` (tuned on 5 Nov at N = 100 in the old benchmark with TPre 0.1; not retuned: tuning is stopped). Oracle = per-hop oracle (best true success x progress), Oracle+power as above with E_tx = 8 W x scale x airtime.
- Result:

PDR (mean of 20 seeds) and the paired gaps (95% CI)

| N | MAC | sources | oracle hop success | RR | RR+FB | FW-phys | FW-tuned | Oracle | Oracle+power | oracle - best FW | oracle - RR+FB | oracle - RR |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 100 | Csma | 10 | 0.684 | 0.443 | 0.746 | 0.819 | 0.819 | 0.843 | 0.845 | +0.023 +- 0.010 | +0.097 +- 0.015 | +0.399 +- 0.020 |
| 100 | Csma | 30 | 0.436 | 0.370 | 0.553 | 0.610 | 0.615 | 0.626 | 0.663 | +0.011 +- 0.008 | +0.073 +- 0.007 | +0.256 +- 0.013 |
| 100 | Aloha | 10 | 0.704 | 0.445 | 0.744 | 0.816 | 0.820 | 0.839 | 0.845 | +0.019 +- 0.009 | +0.095 +- 0.018 | +0.394 +- 0.022 |
| 100 | Aloha | 30 | 0.530 | 0.368 | 0.571 | 0.646 | 0.646 | 0.666 | 0.696 | +0.020 +- 0.010 | +0.095 +- 0.011 | +0.297 +- 0.013 |
| 300 | Csma | 10 | 0.631 | 0.426 | 0.736 | 0.776 | 0.776 | 0.791 | 0.793 | +0.015 +- 0.011 | +0.055 +- 0.014 | +0.365 +- 0.015 |
| 300 | Csma | 30 | 0.432 | 0.337 | 0.544 | 0.588 | 0.587 | 0.602 | 0.615 | +0.014 +- 0.009 | +0.058 +- 0.010 | +0.266 +- 0.010 |
| 300 | Aloha | 10 | 0.652 | 0.428 | 0.722 | 0.763 | 0.767 | 0.786 | 0.774 | +0.019 +- 0.012 | +0.064 +- 0.016 | +0.358 +- 0.017 |
| 300 | Aloha | 30 | 0.504 | 0.338 | 0.554 | 0.605 | 0.597 | 0.616 | 0.637 | +0.012 +- 0.011 | +0.063 +- 0.009 | +0.278 +- 0.008 |


Energy per delivered packet (J; transmit + receive + idle of all ordinary nodes)

| N | MAC | sources | RR | RR+FB | FW-phys | FW-tuned | Oracle | Oracle+power |
|---|---|---|---|---|---|---|---|---|
| 100 | Csma | 10 | 15.7 | 17.2 | 13.3 | 13.4 | 12.8 | 11.1 |
| 100 | Csma | 30 | 13.6 | 20.3 | 17.1 | 16.9 | 17.0 | 13.3 |
| 100 | Aloha | 10 | 15.6 | 16.4 | 12.6 | 12.6 | 12.1 | 10.6 |
| 100 | Aloha | 30 | 13.7 | 17.8 | 14.2 | 14.2 | 13.9 | 11.3 |
| 300 | Csma | 10 | 46.2 | 47.2 | 41.8 | 41.8 | 40.5 | 38.1 |
| 300 | Csma | 30 | 39.3 | 54.5 | 48.2 | 48.4 | 47.9 | 42.7 |
| 300 | Aloha | 10 | 45.9 | 45.8 | 40.4 | 39.6 | 38.7 | 37.2 |
| 300 | Aloha | 30 | 38.9 | 48.6 | 42.2 | 42.8 | 41.9 | 37.6 |

  1. **No scenario reaches an oracle - FW gap of 0.1: the gap is +0.011 to +0.023 (95% CI about +-0.01).** The oracle - RR+FB gap is 0.055-0.097 and the oracle - RR gap 0.26-0.40; so nearly everything between round robin and the oracle is obtained by hop feedback plus the static feature rule (FW); what is left for any learner to win over FW is 1-2 PDR points. The oracle's hop success is 0.43-0.70: the remaining losses are collisions (DATA with DATA and with beacons), which no forwarder choice avoids.
  2. **Energy per delivered packet:** RR is cheapest (no ACKs) but delivers 0.34-0.45; with feedback FW needs 12.6-48 J, the oracle 12.1-48 J (the same), and ACKs and retries cost 5-39% more energy than RR for the extra 0.1-0.4 PDR (0.1 only at 30 sources).
  3. **Oracle with power levels: PDR equal or higher (+0.000 to +0.037; the largest gain at 30 sources: N = 100, Csma 0.626 -> 0.663, Aloha 0.666 -> 0.696) and energy per delivered packet 4-22% lower** (N = 100, Csma, 30 sources 17.0 J -> 13.3 J, -22%; N = 300, Csma, 10 sources 40.5 -> 38.1 J, -6%). So power control is the only place where the oracle beats the static rule by more than noise (PDR +0.02-0.04 at 30 sources, energy -5 to -22%), though still not near 0.1 of PDR. A caution about the mechanism: lowering the power does NOT cut interference in this PHY, because every node within R still gets a copy of every frame and an undecodable frame still counts as a collision (see the step 3 review); the PDR gain at 30 sources therefore comes from somewhere else (less Tx energy, fewer dead receivers) and is not tested here.
  4. Csma vs Aloha now give similar PDR (oracle: Csma +0.004 / -0.040 at N = 100 with 10 / 30 sources, +0.005 / -0.014 at N = 300), as the loss budget predicted.

## 2026-11-06 16:00 — Step 4, fix 1: no preamble in the paper-aligned configs (TPre = 0)
- What: `TPre = 0` in `configs/p0-published.txt` and `configs/g-base.txt` (the spec default 0.1 s stays in the code and in `configs/g-sensitivity.txt`).
- Why: the loss budget showed that the loss is contention (DATA-DATA and beacon collisions) and that contention scales with the airtime, which at 17.8 kbit/s is mostly preamble (25 B beacon: 0.111 s of which 0.100 s preamble; 150 B data: 0.175 s of which 0.100 s). The paper gives no preamble and Aqua-Sim's own default is 0; the spec sweep includes TPre = 0.
- Effect on test 16 (P0, 20 seeds 101-120, CSMA; PDR mean +- 95% CI; `results/reference/test16-fixes/aggregate.csv`):
  | speed (m/s) | paper | TPre 0.1, ideal links | **TPre 0, ideal links** | TPre 0.1, calibrated | TPre 0, calibrated |
  |---|---|---|---|---|---|
  | 0 | 0.875 | 0.644 +- 0.029 | **0.821 +- 0.019** | 0.524 +- 0.030 | 0.631 +- 0.031 |
  | 0.5 | 0.84 | 0.601 +- 0.032 | **0.790 +- 0.022** | 0.500 +- 0.035 | 0.631 +- 0.030 |
  | 1.0 | 0.805 | 0.556 +- 0.028 | **0.761 +- 0.023** | 0.459 +- 0.031 | 0.606 +- 0.029 |
  | 1.5 | 0.752 | 0.520 +- 0.032 | **0.737 +- 0.026** | 0.429 +- 0.036 | 0.593 +- 0.031 |
  Level gap at 0 m/s against the paper: **-0.231 -> -0.054** (ideal links), -0.351 -> -0.244 (calibrated channel, which is our own link model, not the paper's BER 1e-9). Fall from 0 to 1.5 m/s: 0.085 against the paper's 0.123 (ideal links; 0.125 before the fix). Energy per delivered packet (ideal links, 0 m/s): 58.3 J -> 21.1 J. The residual gap (-0.05 at 0 m/s, -0.015 at 1.5 m/s) is inside twice the confidence interval at 1.5 m/s and about 2.5x at 0 m/s.
- Spec/design deviation: a choice for the paper-aligned configs only; the spec default (0.1 s) is untouched, and `TPre` can be swept.

## 2026-11-06 14:00 — Steps 2 and 3: loss budget (P0 and g) and review of the PHY receive model
- What: `model/loss-budget.{h,cc}` (reporting only): every DATA hop attempt gets ONE cause when it did not arrive; every unresolved packet gets a cause (`packets.csv` column `loss_cause`); airtime by frame type; `loss_budget.csv` per run; `scripts/loss_budget.py` (P0 and g, 1 and 10 sources, ideal links, 0 m/s, N = 300, 20 seeds 101-120); `results/reference/loss-budget/`.
- Why: user request of 6 Nov 2026, steps 2 and 3: find out why the network is so lossy.
- Effect on results: none (measurement only).
- How a hop is classified (the attempt is a DATA frame handed to the MAC for a named forwarder; the channel evaluation gives its air time and arrival interval at the forwarder; the forwarder reports arrival): never_on_air (MAC dropped it), out_of_range, per_loss (undecodable), receiver_dead, half_duplex (forwarder transmitting when the frame starts to arrive; this is the PHY's rule), collision with DATA / beacon-Hello / ACK-NACK (another frame overlaps it at the forwarder; Aqua-Sim has no capture; order of naming DATA, beacon/Hello, ACK/NACK when several overlap), unexplained_phy_busy (decodable, nothing overlaps, forwarder not transmitting). "No head known", "void/NegAck" and "hop budget exhausted" are not link failures: they appear as `drop_*` packet causes and in `routing,void_nacks`.
- Result (ideal links, no shadowing, 0 m/s, N = 300, 20 seeds, round robin):
  | | PDR | hop success | failed hops: collision DATA | collision beacon/Hello | collision ACK/NACK | half duplex | receiver dead | unexplained | airtime (Erlang): total / beacon / Hello+election / data |
  |---|---|---|---|---|---|---|---|---|---|
  | P0, 1 source | 0.881 | 0.952 | 3.30% of hops | 1.33% | 0 | 0.10% | 0 | 0.06% | 0.074 / - / 0.041 / 0.033 |
  | P0, 10 sources | 0.644 | 0.843 | 12.04% | 1.66% | 0 | 0.35% | 1.39% | 0.26% | 0.279 / - / 0.042 / 0.238 |
  | g, 1 source | 0.497 | 0.777 | 2.97% | **18.59%** | 0 | 0.35% | 0.04% | 0.33% | 0.387 / **0.315** / 0.049 / 0.023 |
  | g, 10 sources | 0.376 | 0.697 | 10.71% | **17.78%** | 0.02% | 0.44% | 0.57% | 0.77% | 0.536 / **0.311** / 0.053 / 0.171 |
  Full tables (packets by loss cause, airtime shares): `results/reference/loss-budget/loss_budget_ideal_links.md`.
  1. **Every lost percent at one source (P0, PDR 0.881):** 11.9% of the packets are lost: 8.1% to a collision with another DATA frame, 3.6% to a collision with a Hello/Exit frame, 0.2% half duplex, 0.2% unexplained. The DATA-DATA collisions are NOT between hops of the same packet or frames of the same sender (those counts are zero): they are different packets of the same single source, which is a Poisson stream at 0.07 pkt/s (mean gap 14 s) while one packet needs about 7 s to cross the network, so about 40% of the packets overlap with a predecessor, and two flows hear each other's frames at many receivers. With one source PDR cannot be close to 1 in this channel model; it is bounded by about 0.88.
  2. **Ten sources (P0):** hop success 0.843; 76.6% of the failed hops are DATA-DATA collisions (12% of all hops), 10.6% Hello collisions, 8.9% dead receivers (nodes start to die at about 1400 s: 300 J against receive power 0.8 W with every frame heard by about a third of the box), 2.2% half duplex. With 5.5 hops per packet this gives PDR 0.64.
  3. **ERGR-EMHC-g loses mainly to its own beacons:** beacons are 58-81% of its airtime (0.31 Erlang, with lean beacons already on) and 17.8-18.6% of ALL data hops die in a collision with a beacon/Hello; at one source this is 41.7% of the packets. P0 has no beacons; its Hellos take 0.04 Erlang.
  4. Airtime per frame is dominated by the preamble: TPre = 0.1 s means a 25 B beacon lasts 0.111 s (the payload bits only 0.011 s) and a 150 B data frame 0.175 s. See the next entry for the effect of TPre.
  5. Hello period: the paper sends one Hello per cell-head at its election and then every Update Period = 500 s (Table 3, same as GBPR); the other nodes of the cell cancel their initialization timers on hearing it and arm a "new election" timer of 400 s (Table 3). Our P0 does the same (head Hello at election and every 500 s; members' election timer 400 s + iPeriod x P(i)). So, as in the paper, members time out at 400-450 s, before the head's next Hello at 500 s, and the head role rotates about every 420 s; Hello frames come from about 170 heads per 500 s = 0.34/s = 0.04 Erlang, as measured. Nobody else sends Hellos (Exit frames only when a head leaves its cell, none at 0 m/s).
- Step 3, review of the PHY receive model (Aqua-Sim NG `AquaSimPhyCmn::PrevalidateIncomingPkt`, `CollisionCheck`, `AquaSimSignalCache`; nothing changed). I cannot read the ns-2 sources of the paper's simulator here, so the comparison with ns-2 is from the structure of the port (same function names and logic) and is marked as unverified.
  * **2-airtime receive delay (E17):** `PrevalidateIncomingPkt` delays a clean frame by one airtime for `CollisionCheck`, and `AquaSimSignalCache::AddNewPacket` by another (`PktSubmissionTimer`). It delays delivery (latency +0.35 s per hop) and stretches the ACK timeout, but it does not itself lose frames. It does make a collided node busy for one extra airtime: `CollisionCheck` puts the device back in RECV for another airtime after a collision, so a frame arriving in that tail is lost although nothing overlaps it. Measured size: "unexplained_phy_busy" is 0.06% (P0, 1 source) to 0.77% of hops (g, 10 sources): small.
  * **Busy during propagation?** No. The device is RECV only from the moment a frame ARRIVES (after the propagation delay of up to 1 s) for one airtime. A sender therefore cannot sense a frame still travelling, so carrier sense cannot prevent collisions between nodes that are far apart: this is why CSMA instead of Aloha changed the PDR by only 0 to +0.03. Physically right for acoustics.
  * **Every overlap is a collision, no capture:** a frame that arrives while the device is in RECV is marked erroneous, and `m_collision_flag` marks the frame being received as erroneous too; the signal-cache SINR test is `sinr > 0` against a constant receive power, i.e. it never rejects anything. So two overlapping frames are both lost whatever their powers (a frame from 100 m does not survive an interferer from 1400 m).
  * **Interference range = transmission range:** every node within R = 1500 m gets a copy, none beyond. A frame that is below the decode threshold (PER, `Pr < RXThresh`) still sets the collision flag, so with the calibrated channel an undecodable far-away frame destroys a good reception; with `IdealLinks=true` this does not occur. About 35% of the 3 km box hears each frame.
  * **Half duplex (P5):** a frame that starts to arrive while the node transmits is lost (it is the check the NG authors had commented out, upstream ns-2 has it as far as I know).
  * Conclusion: nothing in the PHY inflates losses by an error; the model is simply an ALOHA-like medium with a long vulnerability window (two airtimes of the competing frames plus propagation spread up to 1 s) and no capture. The multiplier is the airtime, i.e. the preamble.

## 2026-11-06 10:00 — Step 1 (E34): members always know their cell's head; the real cause was headless cells
- What:
  1. **Head solicitation** (`Solicit=true`, `params::solicit`): a member with no usable head row of its own cell HOLDS its packet (up to 32), broadcasts a SOLICIT (election-kind frame, 9 B, `ErgrElectionHeader::Solicit`) every 4 s, up to 3 asks; heads of that cell answer with a beacon after U(0, 1 s) (at most once per 2 s); any beacon from an own-cell head (or the member becoming head itself) releases the held packets. After the third unanswered ask it starts an election round and only then gives the packets up. Events `SOLICIT`, `SOLICIT_ANSWER`, `HEAD_FOUND_AFTER_SOLICIT`, `NO_HEAD_GIVE_UP`; summary columns `solicits_sent, solicits_answered, nohead_giveups, nohead_giveups_cell_had_head`.
  2. **Ground-truth head coverage** (reporting only): `cells_with_head_share`, `nodes_in_headless_cells_share` (sampled every 25 s), event `HEADLESS_CELLS`.
  3. **Two election fixes**, found by tracing the cells that stayed headless (cell 46 of seed 101 had 9 nodes and no head for 1800 s):
     * a head retired at an election round when it merely ranked below k in its own table; the better-ranked nodes do not necessarily claim (they claim only in a round they run themselves), and rows of nodes that have left the cell still rank above everybody. Now it retires only if it hears >= k better HEADS (the spec rule D§3 P3 step 3);
     * a member ranked below k never claimed, so when every member saw 5-7 stale "better" rows nobody claimed. Now it claims in its own later slot (rank - 1 s) if fewer than k heads of the cell have been heard by then;
     * a member that knows no head of its cell (heads left, never elected) starts an election round at its next beacon tick (debounced).
  4. Test `E34: no packet dies at the source for 'no own head' while the cell has a head...` (120 nodes, speed 1, lean beacons on: zero ground-truth-wrong give-ups, >= 90% of cells headed, < 10% of nodes headless); 49 cases pass.
- Why: user request of 6 Nov 2026, step 1.
- Effect on results: every ERGR-EMHC-g number from before this entry is superseded (lean beacons on, N = 300, 10 sources, Csma, paper radio).
- Spec/design deviation: the election repairs follow D§3 P3 more closely (step-down by heard heads); the back-up claim and the "no head in my cell" trigger are additions.
- Result (RR, `configs/g-base.txt`, 20 training seeds 101-120; `results/reference/e34/`): the diagnosis in the 5 Nov entry was WRONG in one respect: the solicit alone changed little, because in 99% of the give-ups the cell really had no live head (ground truth). Before: 43% of nodes lived in headless cells (lean on); after the election fixes 0.3%.
  | | PDR | hop success | cells with a head | nodes in headless cells | give-ups for "no head" | of which the cell had a head |
  |---|---|---|---|---|---|---|
  | RR, lean beacons on | **0.243 +- 0.022** (was 0.156 with 5 seeds) | 0.616 | 0.995 | 0.003 | 1.35 per run | 0.30 per run |
  | RR, lean beacons off | 0.165 +- 0.020 | 0.516 | 0.996 | 0.001 | 0.15 | 0.10 |
  So E34 is closed for its stated purpose (a member finds the head when the cell has one: 0.3 exceptions per run are heads elected during the wait), but PDR is still far below P0 (0.43) and below the paper: the next steps look for the other losses.
- From now on every compared number uses 20 seeds: training seeds 101-120 (the test seeds 1-20 stay untouched).

## 2026-11-05 18:00 — Step C: warm start check, ExplorationScale tuning, ProbStd ACK gate, heterogeneous scenario, density transfer
- What:
  * **C9 warm start:** checked, no code change needed. `PriorWeights` already sets BOTH `thetaHat` and the cell prior `thetaCell` (`selector-cb-v5.cc` SetServices: `SetCellPrior(w); SetTheta(w)`), and `BeginDecision` decays theta toward `thetaCell`. The only later writer of the cell prior is `RefreshCellPrior`, which replaces it by the mean of accepted same-cell peers' models (themselves warm). So the explanation "forgetting decays toward zero" does not hold; a different reason is likely (see result 1).
  * **C10:** `scripts/tune.py` CB range `ExplorationScale` 0.2-1.0 (was 0.05-4); `--config/--extra` so tuning runs on a scenario config.
  * **C11:** `AckGate=Logit|ProbStd` (CB attribute, default Logit = the spec gate, TauW = 0.3 unchanged) and `ProbStdThreshold` (`params::learner::kAckProbStdThreshold`, 0.05 default; 0.2 tuned, see below). ProbStd asks for an ACK when p (1 - p) width(x) > threshold, p = the chosen option's mean success.
  * **C12:** heterogeneous scenario `--NoiseBandOffsetDb=10`: receivers with x in [750, 1500) m (the second cell column) hear 10 dB more noise (`model/noise-band.h`, applied in `CbErgrPropagation` and `OracleEvaluator`; CONFIRM E33). The beacon SNR a forwarder measures is the reverse link at ITS position, so the band is invisible to FW's and CB's features; only the channel and the oracle know it.
  * `scripts/compare.py` (variants on a config, paired differences), `configs/{fw,cb}-tuned-v2{,-n300}.txt`; tests for the ProbStd gate and the noise band (48 + 1 cases pass).
- Why: user items C9-C12 and R8 (train on one density, test on another).
- Effect on results: defaults unchanged (`AckGate=Logit`, `NoiseBandOffsetDb=0`).
- Spec/design deviation: the ProbStd gate is an ablation beside the spec gate, not a replacement. The tuned FW/CB settings below are for the new benchmark (N = 100, 10 sources, Csma, 2000 s, lean beacons, 5 seeds 101-105, 40 evaluations each, same optimiser); FW had to be retuned because the old tuning belonged to the old benchmark.
- Setup of all runs below: `configs/g-base.txt` with `--N=100` (N = 300 where stated), 10 sources, Csma, validation seeds 106-110, PDR mean +- 95% CI of 5 seeds; paired differences are per seed.
- Result 1, CB-warm vs FW (homogeneous, N = 100):
  | method | PDR | hop success | regret | AR rate | ACK frames / delivered |
  |---|---|---|---|---|---|
  | RR | 0.303 +- 0.023 | 0.686 | 0.184 | 0 | 0 |
  | RR + feedback | 0.436 +- 0.018 | 0.296 | 0.128 | 1.00 | 7.22 |
  | FW physics | 0.497 +- 0.042 | 0.316 | 0.038 | 1.00 | 6.64 |
  | FW old tuning (before the benchmark change) | 0.482 +- 0.048 | 0.315 | 0.041 | 1.00 | 6.71 |
  | FW tuned now (v2) | 0.460 +- 0.031 | 0.310 | 0.040 | 1.00 | 6.98 |
  | CB defaults | 0.449 +- 0.042 | 0.304 | 0.094 | 1.00 | 6.88 |
  | CB tuned (v2) | 0.457 +- 0.058 | 0.300 | 0.101 | 1.00 | 7.00 |
  | CB-warm (tuned CB + FW-v2 weights) | 0.465 +- 0.051 | 0.302 | 0.083 | 1.00 | 6.85 |
  | CB-warm, Lambda0 = 10 (diagnostic) | 0.459 +- 0.083 | 0.307 | 0.072 | 1.00 | 6.92 |
  | Oracle | 0.466 +- 0.053 | 0.320 | 0 | 1.00 | 7.04 |
  Paired against FW-v2: CB-warm +0.006 +- 0.066, CB tuned -0.003 +- 0.057, FW physics +0.038 +- 0.031, Oracle +0.007 +- 0.060. **CB-warm equals FW (difference +0.006, CI +-0.066); everything from FW to the oracle is inside the noise; retuning FW made it worse out of sample (0.497 -> 0.460, overfitting 5 seeds).** The oracle is no better than FW, so there is nothing to learn here (headroom table). A likely reason the warm start fades is the prior precision: Lambda0 = 1 (tuned 2.3) against FW weights up to 30 means the first label steps and the exploration noise (std ~ V / sqrt(Lambda0) per weight) are as large as the weights themselves; the design's P7 prior precision (0.1 x Laplace Hessian) is not built, and the Lambda0 = 10 row shows no gain either.
- Result 2, tuned ExplorationScale: the (1+1)-ES explored V in [0.43, 1.0] and kept **V = 1.0** (tuned CB: V = 1, Gamma = 0.948, Lambda0 = 2.34, KappaP = 0.328; in-sample PDR 0.482 -> 0.521). The top five evaluations used V between 0.43 and 1.0, i.e. no preference. Validation with the other settings fixed: V = 1.0 0.457 +- 0.058, V = 0.5 0.445 +- 0.066, V = 0.2 0.460 +- 0.059: **no measurable effect of V in 0.2-1.0**. What the tuning did move is Lambda0 (1 -> 2.3-3.9 in all top candidates).
- Result 3, ACK gates (CB tuned; tuning seeds 101-105 for the threshold, validation seeds 106-110):
  | gate | PDR (val) | AR rate | ACK frames / delivered | ACK energy J / delivered | hop success |
  |---|---|---|---|---|---|
  | always ACK (AckMode=Every) | 0.457 +- 0.058 | 1.000 | 7.00 | 10.81 | 0.300 |
  | spec logit-width gate, TauW = 0.3 | 0.457 +- 0.058 | **1.000** | 7.00 | 10.81 | 0.300 |
  | ProbStd, threshold 0.2 | **0.502 +- 0.053** | 0.728 | **4.70** | **7.63** | 0.404 |
  | BitmapOnly (retries still ask) | 0.501 +- 0.085 | 0.638 | 4.23 | 6.89 | 0.428 |
  The threshold was chosen on seeds 101-105 as the largest of {0.01, 0.02, 0.05, 0.1, 0.15, 0.2, 0.3} whose PDR stays within 0.01 of always-ACK (0.2: 0.526 against 0.520, AR 0.750); 0.3 lost 0.014. **The spec gate never relaxes (AR 1.000, identical to Every); ProbStd at 0.2 saves 33% of the ACK frames and 29% of the ACK energy and RAISES PDR by +0.046 +- 0.029 (paired) because fewer ACKs mean less contention.** The PDR gain is a channel-load effect, not a learning effect (BitmapOnly gets the same).
- Result 4, heterogeneous scenario (N = 100, +10 dB in the band; invisible to the features):
  | method | PDR | hop success | AR rate | ACK frames / delivered |
  |---|---|---|---|---|
  | RR | 0.144 +- 0.042 | 0.574 | 0 | 0 |
  | RR + feedback | 0.261 +- 0.047 | 0.248 | 1.00 | 9.87 |
  | FW physics | 0.263 +- 0.054 | 0.269 | 1.00 | 9.91 |
  | FW tuned on the homogeneous scenario (v2) | 0.269 +- 0.039 | 0.270 | 1.00 | 9.82 |
  | CB tuned | 0.267 +- 0.046 | 0.259 | 1.00 | 9.69 |
  | CB-warm | 0.276 +- 0.046 | 0.280 | 1.00 | 9.58 |
  | CB tuned + ProbStd | 0.286 +- 0.038 | 0.338 | 0.73 | 6.91 |
  | CB-warm + ProbStd | 0.301 +- 0.054 | 0.353 | 0.69 | 6.26 |
  | Oracle | 0.306 +- 0.035 | 0.277 | 1.00 | 9.86 |
  Oracle - FW(v2) = +0.038 +- 0.020 (it was +0.007 without the band), so the band creates a little headroom but far less than 0.1. Paired against FW(v2): CB tuned -0.002 +- 0.073, CB-warm +0.007 +- 0.067, CB-warm + ProbStd +0.033 +- 0.073. **CB (cold or warm) does not beat FW in the heterogeneous scenario either.** Reason: the band changes which CANDIDATE is bad, and no feature identifies the candidate's cell; CB's model is per head, so it can only shift its intercept.
- Result 5, density transfer (R8; N = 100 versus N = 300, validation seeds): FW weights tuned at N = 100 give PDR 0.219 +- 0.050 at N = 300, against 0.276 +- 0.045 for weights tuned at N = 300 and 0.262 +- 0.062 for the physics weights (oracle 0.258 +- 0.026): **a transfer loss of 0.057 for FW**. The other way, N = 300-tuned FW at N = 100: 0.477 +- 0.039 against 0.460 +- 0.031 (tuned at N = 100) and 0.497 +- 0.042 (physics). CB at N = 300: hyperparameters tuned at N = 100 0.250 +- 0.053, tuned at N = 300 (V = 0.92, Gamma = 0.959, Lambda0 = 0.60, KappaP = 0.48) 0.245 +- 0.059, CB-warm with FW(N=100) 0.278 +- 0.053, CB-warm with FW(N=300) 0.245 +- 0.036, FW(N=300) 0.276 +- 0.045. At N = 100, CB with N = 300 hyperparameters 0.488 +- 0.098 against 0.457 +- 0.058. So the weights do not transfer between densities for FW (-0.057), while CB's online learning is not visibly better or worse after transfer (all CB differences inside the noise); this is the only place where a difference between FW and CB shows, and it is a statement about the FW tuning, not a win for CB (the physics weights transfer best).

## 2026-11-05 14:00 — Step B: headroom table
- What: `scripts/headroom.py` (runs RR, RR+FB, FW-phys, FW-tuned and the Oracle for N in {100, 300} x Mac in {Csma, Aloha} x NumSources in {10, 30}, 5 seeds 106-110, `configs/g-base.txt` otherwise, lean beacons on); results in `results/reference/headroom/`.
- Why: user item B8: measure how much room there is above the static rule before comparing methods again; only scenarios with an oracle - FW gap of roughly >= 0.1 are meaningful for "does learning help".
- Effect on results: none (measurement only). FW-tuned = the weights of `configs/fw-tuned.txt`, which were tuned on the OLD benchmark (5 kbit/s, Aloha, N = 100); they are not retuned here.
- Result (PDR mean of 5 seeds; oracle hop success = ground-truth share of data attempts the chosen forwarder really received):

| N | MAC | sources | oracle hop success | oracle PDR | FW-phys | FW-tuned | RR+FB | RR | oracle - FW (best) | oracle - RR+FB | oracle - RR |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 100 | Csma | 10 | 0.32 | 0.466 | 0.497 | 0.482 | 0.436 | 0.303 | -0.031 | 0.03 | 0.163 |
| 100 | Csma | 30 | 0.181 | 0.267 | 0.257 | 0.254 | 0.231 | 0.22 | 0.01 | 0.035 | 0.047 |
| 100 | Aloha | 10 | 0.48 | 0.54 | 0.521 | 0.505 | 0.466 | 0.306 | 0.019 | 0.074 | 0.234 |
| 100 | Aloha | 30 | 0.334 | 0.291 | 0.29 | 0.291 | 0.264 | 0.216 | 0.0 | 0.028 | 0.076 |
| 300 | Csma | 10 | 0.312 | 0.258 | 0.262 | 0.275 | 0.22 | 0.174 | -0.016 | 0.038 | 0.084 |
| 300 | Csma | 30 | 0.22 | 0.157 | 0.152 | 0.15 | 0.149 | 0.124 | 0.005 | 0.008 | 0.033 |
| 300 | Aloha | 10 | 0.49 | 0.31 | 0.3 | 0.303 | 0.298 | 0.162 | 0.008 | 0.012 | 0.149 |
| 300 | Aloha | 30 | 0.381 | 0.186 | 0.188 | 0.196 | 0.169 | 0.119 | -0.009 | 0.018 | 0.067 |

  **No scenario reaches an oracle - FW gap of 0.1: the gap is between -0.03 and +0.02 everywhere** (the oracle is a per-hop oracle; it does not beat FW on PDR, in three cells it is below it). The oracle's hop success is only 0.18-0.49, so most hops are lost for reasons no forwarder choice can change (collisions, member hand-offs without a known head, see the step A entry). The gap to plain round robin is 0.03-0.23, and to round robin with hop ACKs 0.01-0.07, so what separates the methods is mostly having feedback and the feature rule, not learning the weights. Largest oracle - RR gaps: N = 100, 10 sources (0.16 with Csma, 0.23 with Aloha). Consequence: none of the 8 scenarios can show that learning helps; the CB-vs-FW comparisons below are reported for completeness and for the heterogeneous scenario, which is built to create headroom.

## 2026-11-05 12:00 — Step A: the benchmark follows the paper (CSMA, paper radio and traffic, P0 unchanged, lean beacons, equal-Hmin fallback, lifetime)
- What:
  1. **ERGR-EMHC-g now uses P0's settings** (`configs/g-base.txt`): 17.8 kbit/s, 8 W / 0.8 W / 0.008 W, 300 J, 3 sinks, N = 300, `NumSources` = 10 at 0.07 pkt/s each, 2000 s; data from 300 s and the first 300 s discarded (the election start-up needs ~55 s + 2 s x cell id; the paper discards 150 s). The old low-rate setting is `configs/g-sensitivity.txt` (sensitivity runs only). `sweeps/numsources-{p0,g}.yaml` sweep the number of sources over {10, 15, 30, 45}.
  2. **MAC: `Mac=Csma|Aloha`** (scenario setting, default `Csma` = `params::mac::kMac`). The paper says "a CSMA-based MAC protocol" (section 5; no RTS/CTS mentioned), recorded in CONFIRM E30. Aqua-Sim NG has no plain CSMA MAC (`AquaSimBroadcastMac` senses the channel but backs off at most 0.1 s four times and then silently drops the frame; FAMA/COPE-MAC are handshake protocols). So `cbergr::CsmaMac` (`model/csma-mac.{h,cc}`, in our module, no upstream change, so nothing new in PATCHES.md) is a minimal listen-before-talk: FIFO of 64 frames, random backoff in [0, CW) slots of 0.1 s, CW = 8 (2 for ACK/NACK), doubling to 128 when the modem is busy (sending or receiving), drop after 16 deferrals. All numbers are [CONFIRM] (`params::mac`). `Mac=Aloha` is the old behaviour. `summary.csv` gains `mac_drops`, `mac_deferrals`.
  3. **P0 election timers: `RandomTimers=false`** (the published eq. 4 priority timer). The earlier "random timers" instruction was withdrawn; P0 is the published protocol unchanged (E29).
  4. **`LeanBeacons` defaults to true** for ERGR-EMHC-g (`params::timers::kLeanBeacons`); `LeanBeacons=false` is the leave-one-out run.
  5. **Eligibility (E11): equal-Hmin fallback** (`EqualHminFallback`, default true): when no strictly better forwarder is eligible, a head with the same Hmin as mine may be used while Hpkt allows another hop (Hmin_j < Hpkt); suspects still come last in each pass.
  6. **Target lifetime (B7):** CB's `TargetLifetimeS` = `TargetLifetimeFactor` (2.0) x SimTimeS in every scenario (explicit `SelectorAttributes` override it).
  7. Diagnostic event `NO_HEAD_FOR_HANDOFF` (what the member's table says when it has no head to hand a packet to).
- Why: user decisions A1-A7 of 5 Nov 2026 (the P0 -> g comparison must change the protocol only; the paper's MAC).
- Effect on results: every ERGR-EMHC-g number produced before this entry used the Aloha MAC, 5 kbit/s radio, lean beacons off and no fallback; they are historical (the sweep files that made them say so).
- Spec/design deviation: `LeanBeacons` default on (D§4.2 N-C made default, user decision); the CSMA parameters are mine.
- Result 1, test 16 re-run (P0, 8 seeds 101-108, N = 300, 2000 s; PDR mean +- 95% CI; `results/reference/test16-v2/`):
  | speed (m/s) | paper | CSMA, ideal links | CSMA, calibrated | Aloha, ideal links | Aloha, calibrated |
  |---|---|---|---|---|---|
  | 0 | 0.875 | 0.629 +- 0.068 | 0.507 +- 0.063 | 0.603 +- 0.069 | 0.491 +- 0.063 |
  | 0.5 | 0.84 | 0.570 +- 0.062 | 0.465 +- 0.078 | 0.558 +- 0.053 | 0.469 +- 0.065 |
  | 1.0 | 0.805 | 0.526 +- 0.047 | 0.422 +- 0.056 | 0.510 +- 0.050 | 0.424 +- 0.054 |
  | 1.5 | 0.752 | 0.497 +- 0.051 | 0.396 +- 0.066 | 0.484 +- 0.063 | 0.389 +- 0.060 |
  **Level gap at 0 m/s: -0.25 (CSMA, ideal links) and -0.37 (calibrated channel) against the paper.** The trend is reproduced (fall of 0.13 / 0.11 over 0 -> 1.5 m/s, -21% / -22%, paper -0.12, -14%). CSMA instead of Aloha changes the level by only +0.00 to +0.03: carrier sense cannot help much when propagation takes up to 1 s and one frame lasts 0.17 s, so hidden transmissions still collide. What does matter is the number of simultaneous sources: with ideal links and ONE source at 0.07 pkt/s PDR is 0.82 (hop success 0.957), with ten sources 0.54 (hop success 0.835). The paper's 0.875 with ten sources needs a much less contended channel than Aqua-Sim's PHY collision model gives here; the cause was not found (packet size is the same 150 B).
- Result 2, load sweep (P0 and g, 5 seeds, `results/reference/numsources-*/`): PDR falls with the number of sources in both, as in the paper (P0 calibrated 0.43 / 0.39 / 0.32 / 0.26 for 10 / 15 / 30 / 45 sources; paper 0.82 / 0.71 / 0.615 for 15 / 30 / 45).
  **With the paper's settings ERGR-EMHC-g is much worse than P0:** round robin PDR 0.156 / 0.159 / 0.141 / 0.111 against P0's 0.43 / 0.39 / 0.32 / 0.26; g with hop ACKs (RR+FB) 0.25 / 0.22 / 0.15 / 0.11; FW (physics weights) 0.25 / 0.22 / 0.17 / 0.12; oracle 0.29 / 0.25 / 0.18 / 0.12.
  Cause found for g round robin (seed 101, 10 sources): 480 of 1119 packets (43%) die at the SOURCE with zero hops, because the member has no usable head row (`NO_HEAD_FOR_HANDOFF`: in 483 of 521 cases the table has rows from its own cell but no head among them). With `LeanBeacons=false` this falls to 142 void drops (PDR 0.105 -> 0.097, since silent TTL losses grow instead), with Aloha instead of Csma it does not improve (576). It is a head-discovery gap of the lean-beacon design (head beacons stretch to 100 s, a member that misses one waits; Hello frames do not create a table row). Not fixed, because the instruction was to report before building anything new.

## 2026-11-05 06:00 — Quorum dead-head detection (D§3 P4)
- What: routing attribute `QuorumDetection` (default **false**) with `DeadCheckPeriodS`, `DeadGuardS`, `DeadQuorum`, `DeadReportTtlS`, `DeadReportRepeatS`, `DeadReportJitterS` (all defaults in `params::deadhead`); election-frame kind `DeadReport` (same 9 B; nodeId = reporter, cellId = the silent head's cell, scoreQ = the silent head's id); `ErgrEmhcRouting::DeadCheckTick/SendDeadReport/OnDeadReport/DeclareDead/IsOverdue/IsRelevantHead`; `MetricsCollector::OnDeadDeclared` and summary columns `dead_declarations, dead_false_declarations, dead_by_quorum, dead_by_expiry, dead_heads_detected, dead_first_latency_mean_s`; scenario settings `QuorumDetection, DeadQuorum, DeadGuardS, DeadReportRepeatS`; `scripts/quorum_grid.py`; 1 test (48 cases pass).
- Why: the user's last instruction ("next build the quorum dead-head detection"); design D§3 P4: a head is DEAD when its beacon is overdue past its advertised next-beacon time + guard and a quorum of any 2 observers agrees.
- How it works: every node (every 10 s) looks at the head rows it can judge: the heads of its OWN cell (co-head and members are the design's observers) and a head of a neighbouring cell only while that head is suspect (two ACK timeouts = the design's "upstream head with timeout evidence", and then the guard is dropped). An overdue row makes the node an observer: it broadcasts a DEAD-REPORT after a random delay (at most once per `DeadReportRepeatS`). A node declares the head dead when its own row is overdue AND it holds `DeadQuorum - 1` fresh reports from other nodes (a node that still hears the head ignores reports, so one node with a bad link cannot kill a live head). Declaring = remove the row, `NodeLost` to the selector, new election if it was a head of the own cell. Any beacon from the head cancels reports about it. The old table expiry (3 beacon periods) stays as the backstop in every mode.
- Effect on results: none by default. `HEAD_DEAD` events of the old path now say "(table expiry)".
- Spec/design deviation:
  1. N-B (members sleep) is not built, so "members at their next wake-up" are simply members that are awake.
  2. Only own-cell heads (and suspect neighbour heads) are watched; the old expiry path is counted over the same population so the two are comparable. (Counting every head in the table gave 4000 "dead" declarations per run for the old rule alone, almost all marginal far links that dropped out and came back.)
  3. Ground truth for the accounting (is the subject really dead, since when) is read from the energy models inside `MetricsCollector` only; the routing never sees it.
- Result (N = 100, 3000 s, RR+feedback, calibrated channel, 10 heads killed at 1500 s, seeds 106-110, per-run means; `results/reference/quorum/grid.txt`):
  | rule | declarations | of which about a live head | killed heads detected (of 10) | mean latency of the first declaration | PDR |
  |---|---|---|---|---|---|
  | table expiry only (backstop) | 207 | 185 | 9.4 | 121 s | 0.497 |
  | quorum 2, guard 10 s, repeat 10 s | 911 | 890 | 9.4 | 73 s | 0.414 |
  | quorum 2, guard 25 s, repeat 10 s | 819 | 796 | 9.6 | 78 s | 0.416 |
  | quorum 2, guard 50 s, repeat 10 s | 404 | 380 | 9.8 | 90 s | 0.452 |
  | quorum 2, guard 50 s, repeat 50 s | 341 | 317 | 9.8 | 85 s | 0.476 |
  | quorum 3, guard 60 s, repeat 10 s | 315 | 292 | 9.2 | 111 s | 0.453 |
  **The quorum detects a dead head 30-50 s earlier (121 s to 73-90 s) but declares live heads dead 2-5 times as often, and PDR falls by 0.02-0.08.** The false declarations are beacon losses: with the calibrated channel (shadowing, PER near the range edge) and broadcast collisions, two nodes missing the same beacon is common, and each false declaration costs a spurious election. Making the guard longer removes the false declarations and the speed-up together (guard 60 s with quorum 3 is no faster than the backstop). So with this channel and load the quorum as specified does not pay off; it stays off by default (`QuorumDetection=false`). The detection that matters for PDR, rerouting around a dead forwarder, is the suspect tier (two ACK timeouts, tens of seconds), which is independent of this rule. A better rule would need evidence that is not subject to the same losses (e.g. requiring observers with a high predicted link success to the head, or an ACK-based probe); not built.
  Test: with a quorum of 1000 no quorum declaration ever happens (a lone observer never declares); with quorum 2 the killed head is declared within 150 s; with the flag off there are no reports.

## 2026-11-05 03:00 — Warm-started CB (R13), speed 0 vs 1, head failures; the benchmark is collision-limited
- What: `CbV5` attribute `NeighbourPrior` (R13b; `PriorWeights` = R13a existed) and service `isNeighbourCell`; `scripts/hop_windows.py`; `sweeps/warmstart-speed-failures.yaml`, `sweeps/warmstart-failures-heavy.yaml`; `.gitignore` for raw sweep output (only `results/reference/` is tracked; the 7.6 MB of raw `results/test16` that went into the previous commit is removed from the index, not from history); test `R13: NeighbourPrior ...` (47 cases pass).
- Why: instruction of 6 Nov: add warm-started CB (R13) and run it and FW under CurrentSpeed 0 vs 1 and with head failures.
- What R13 means here: (a) `PriorWeights`: begin at, and forget toward, the tuned FW weights; (b) `NeighbourPrior`: a node that becomes head with no handover and no model from its own cell starts from the evidence-weighted mean of the models it heard on beacons from the heads of the (26) neighbouring cells (the evidence is averaged, not added; unplanned-import scaling x0.7 as for a takeover). "Neighbour" = grid neighbour, not just radio range.
- Effect on results: none by default (`NeighbourPrior` false).
- Spec/design deviation: none beyond the interpretation above.
- Setup: `configs/g-base.txt` (N = 250, 4 sinks, 750 m cells, 2 heads per cell, E0 10000 J, 0.004 pkt/s), 4000 s, warm-up 500 s, held-out training seeds 106-110, CB hyperparameters = the tuned ones for all CB rows; failures = at 2000 s a random head dies in each of 10 (or 40) random cells that hold at least two live nodes.
- Result (PDR, mean +- 95% CI, 5 seeds; hop success from `hop_windows.csv`):
  | | speed 0 | speed 1 | 10 head failures | hop success 300-800 s (speed 1) |
  |---|---|---|---|---|
  | RR+FB | 0.170 +- 0.006 | 0.188 +- 0.005 | 0.192 +- 0.011 | 0.202 |
  | CB (defaults) | 0.200 +- 0.020 | 0.211 +- 0.017 | 0.214 +- 0.018 | 0.210 |
  | CB-tuned | 0.205 +- 0.024 | 0.210 +- 0.012 | 0.215 +- 0.020 | 0.209 |
  | CB + warm FW weights | 0.191 +- 0.018 | 0.205 +- 0.015 | 0.210 +- 0.010 | 0.220 |
  | CB + neighbour prior | 0.207 +- 0.019 | 0.209 +- 0.013 | 0.217 +- 0.015 | 0.209 |
  | CB + both | 0.189 +- 0.018 | 0.207 +- 0.014 | 0.210 +- 0.012 | 0.220 |
  | FW tuned | 0.218 +- 0.010 | 0.228 +- 0.009 | 0.230 +- 0.009 | 0.219 |
  | FW physics | 0.226 +- 0.011 | 0.224 +- 0.008 | 0.229 +- 0.013 | 0.216 |
  | Oracle | 0.225 +- 0.006 | 0.230 +- 0.010 | 0.239 +- 0.007 | 0.216 |
  1. **The warm start from the FW weights helps early and nothing later.** Hop success in 300-800 s: 0.220 against 0.209 for CB-tuned (speed 1; 0.215 against 0.207 at speed 0), the gap is gone by ~1500 s, and the final PDR is not higher (0.191 against 0.205 at speed 0; inside the CI). Its regret is lower (0.137 against 0.153 at speed 0) but regret is not PDR here. The neighbour prior changes nothing measurable (no head change without a handover happens often enough, and the neighbours' models are themselves weak).
  2. **CurrentSpeed 0 vs 1 changes no ranking.** FW-tuned/FW-physics > CB-tuned ≈ CB ≈ warm CB > RR+FB at both speeds; PDR is a little higher at speed 1 for the feedback methods (+0.01 to +0.02; not significant).
  3. **10 head failures: no measurable effect** (hop success differs by less than 0.012 from the no-failure runs in every window, for every method): with 2 heads per cell, killing one head of 10 cells in ~64 leaves a live co-head and the suspect tier skips the dead one after two ACK timeouts. The recovery metric is therefore undefined (censored) for all methods.
  4. **40 head failures: PDR goes UP by 0.023-0.029 for every method including the oracle** (e.g. CB-tuned 0.210 to 0.235), after a dip of -0.01 to -0.02 in hop success in the first 150 s. Removing 40 of 250 nodes' worth of heads removes channel contention. See the next point.
  5. **The N = 250 benchmark is limited by collisions, not by link quality.** With `IdealLinks=true` and `ShadowSigmaDb=0` (no PER at all) the oracle's hop success is 0.252 (0.235 with the calibrated channel); at N = 100 it is 0.43, and 0.595 at one eighth of the traffic. So three quarters of the data hops at N = 250 fail to collisions/half-duplex (no carrier sense in the Aqua-Sim broadcast MAC; every ACK, beacon and data frame is heard by about half of the 3 km box at 5 kbit/s). What a forwarder-selection method can change is the remaining margin (oracle 0.23 PDR against FW 0.22-0.23): this is why no learner can show a large effect at this size, and why killing nodes helps. This is the most important finding for the supervisor: the protocol comparison needs either a MAC with carrier sense (as in the paper) or a lower load; the choice is not mine (CONFIRM E31).

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
  4. **2000 s vs 10000 s:** CB's regret falls from 0.133 to 0.104 (tuned: 0.132 to 0.098) while FW's stays at 0.04-0.05, so CB does learn, but its PDR gain is small (+0.013) and the gap to FW does not close: FW - CB = 0.035 +- 0.020 (2000 s) and 0.032 +- 0.031 (10000 s) (paired difference over 5 seeds, 95% t interval). FW - CB-tuned = 0.023 +- 0.052 and 0.028 +- 0.033. The ordering FW ≈ A1 > CB-tuned > CB > RR+FB holds at both horizons. PDR of the methods with feedback rises a little with the horizon (+0.005 to +0.03); the oracle's moves by -0.005, i.e. not at all. (Why the share of two-viable decisions falls from 0.69 to 0.60 at 10000 s was not investigated.)
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
