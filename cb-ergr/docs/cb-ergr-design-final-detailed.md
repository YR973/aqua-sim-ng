# CB-ERGR: Final Design (v5.2), Detailed Version

**Yousef Roudani**, MSc thesis, Department of Computer Science, Sultan Qaboos University
**Supervisor:** Prof. Abderezak Touzene
**Date:** 21 October 2026. **Status:** for supervisor review. Not yet confirmed: see the open questions in §10.

> **For Claude, in later sessions.** This file is the single source of truth for the design as of 21 Oct 2026. It supersedes the decision content of `cb-ergr-design-status.md`, `cb-ergr-v5.1-changes.md` and `cb-ergr-v5-supervisor-changes-review.md`. The implementation spec `cb-ergr-v5-implementation-spec.md` (15 Oct) is still the reference for v5 internals: formulas, headers, timers and tests. Where the two disagree, this file wins. Status tags used below: **AGREED** (supervisor approved), **PROPOSED** (requested by the supervisor, reviewed, awaiting his confirmation), **NEW** (our proposal, reviewed), **DROPPED**.

*(Copy note: this file was supplied as chat text in the implementation session of 4-6 Nov 2026 and saved here verbatim so that later sessions can read it from `docs/`.)*

---

## 0. Summary

| Item | Detail |
|---|---|
| **Aim** | Make the department's grid protocol ERGR-EMHC learn which neighbouring cell-head and transmit power to use for each packet. Keep the 3D grid. Change as much else as is useful, as the supervisor asked. |
| **Contributions** | **N1:** per-hop contextual-bandit forwarding (forwarder + power) inside a multi-hop UWSN routing protocol. **N2 (narrowed):** ACK requests gated by the learner's own uncertainty, tied to per-hop forwarding, with an audit rate and handling of label bias. **N3:** sharing the bandit model within a cell, with handover between heads. |
| **Everything else** | Engineering adopted from the literature, with citations, applied to **all** methods. |
| **Headline comparison** | P0 published ERGR-EMHC (RR) → ERGR-EMHC-g + RR (gain from engineering) → ERGR-EMHC-g + CB (gain from learning). |
| **Do first** | A calibration study in weeks 1–2. With the published 300 J battery and 0.75 W receive power, nodes die within the first hour. The cause is the energy budget and offered load, not the protocol. |
| **Scope** | 22 weeks. Tiered as must / should / could, with gates. The learner must start by week 8. |

---

## 1. AGREED: CB-ERGR v5 (approved idea; spec of 15 Oct)

| Part | ERGR-EMHC (base) | CB-ERGR v5 |
|---|---|---|
| Grid, cells | 3D cubic cells | Unchanged |
| Cell head | One per cell; chosen by energy and distance to the cell centre | Changed in v5.1 (§3) |
| Neighbour table (G1) | Heads of neighbouring cells | Adds battery, queue, position/velocity, entry age |
| Hop count | Hmin per node; Hpkt set by the source; rule Hmin ≤ Hpkt | Unchanged |
| **Forwarder choice** | Round robin | **Contextual bandit**: discounted logistic model, Laplace–Thompson sampling over (forwarder, power) arms, 9 features |
| **Transmit power** | Full power | 3 levels per packet; feature: predicted SNR margin |
| **Energy** | Not part of the decision | Online dual energy price + penalty on the forwarder's residual battery |
| **Feedback** | NACK for voids only | Receipt bitmap on head beacons (every packet gets a label) + ACK requested only when uncertain (width > 0.3) + 10% audit |
| **Knowledge sharing** | None | Model shared on beacons when it has changed enough; handed over when the head changes |
| Voids | NACK + retransmission | Unchanged; a void NACK counts as a failure label |

---

## 2. Literature and novelty (checked 19–21 Oct)

### 2.1 Prior art the engineering changes rely on (cited, not claimed)

| Topic | Source | Verified |
|---|---|---|
| One cell per hop, 3D (face neighbours, d = R/√6) | **GARP**: Day, Touzene, Arafeh, Alzeidi, IJCNC 9(5):71–82, 2017, doi 10.5121/ijcnc.2017.9506 | Yes |
| Multi-cell hops (32 cells, d = R/(2√3)); election timers, retire/exit packets | **GBPR**: Al-Salti, Alzeidi, Day, Arafeh, Touzene, IJCNC 9(6), 2017, doi 10.5121/ijcnc.2017.9601 | Yes (random election timing quoted; timer values not given) |
| Virtual grid, one active node per cell, others sleep; 2D rule r ≤ R/√5 | **GAF**: Xu, Heidemann, Estrin, ACM MobiCom 2001 | Yes (2D only) |
| Backup cluster heads underwater | Ovaliadis, Savage, Tsiantos, IEEE TEMU 2014, pp. 167–172, doi 10.1109/TEMU.2014.6917755; Salami et al., IJSECS 8(1):29–44, 2022, doi 10.15282/ijsecs.8.1.2022.4.0094 | Yes |
| Timer-based selection and its collision window | Bletsas, Khisti, Reed, Lippman, IEEE JSAC 24(3), 2006 (arXiv cs/0510071) | Yes |
| Adaptive beacon suppression | **Trickle**: Levis, Patel, Culler, Shenker, USENIX NSDI 2004, pp. 15–28 | Yes |
| Overhearing avoidance (related to header-first receive; S-MAC sleeps on RTS/CTS, it does not decode the header first) | **S-MAC**: Ye, Heidemann, Estrin, IEEE INFOCOM 2002, pp. 1567–1576 | Yes |
| Underwater duty cycling | UWAN-MAC: Park, Rodoplu, IEEE JOE 32(3):710–720, 2007; T-Lohi: Syed, Ye, Heidemann, INFOCOM 2008 | Abstracts |
| Delayed / cumulative ACK | RFC 1122 §4.2.3.2 (ACK at least every 2nd segment, < 0.5 s delay); 802.11 Block ACK; **CARP**: Basagni, Petrioli, Petroccia, Spaccini, Ad Hoc Networks 34:92–104, 2015 (cumulative bitmask ACK, PONG bitmasks, packet trains) | Yes |
| Void avoidance in grids | VA-GMPR, Wireless Sensor Network 10(7):131–156, 2018 (SQU); GEDAR, IEEE TC 65(2):548–561, 2016 | Partly (VA-GMPR authors/DOI still to check) |
| Learner warm start | CLORP, IEEE Sensors J. 2024 (Q-initialisation from location + neighbour count); Wang et al., IEEE Sensors J. 24(7), 2024 (layer-based) | Yes |
| RL routing with hierarchy | QHRP: Rahmani et al., Comput. Electr. Eng. 123:110211, 2025: Q-learning tree, no cells/heads/ACK scheme/sharing in the abstract | Abstract (low overlap) |
| **Unread** | "Toward Efficient Virtual Cell-Based Topology Management and Adaptive Routing for UWSNs", MDPI *Network* 6(2):30, 2026, doi 10.3390/network6020030 | **No.** Blocked twice. Could overlap with P1–P3. Get it through the library or the supervisor. |

### 2.2 Effect on the claims

| Claim | Status after review |
|---|---|
| **N1** | Intact. Closest prior work: Dai 2024 (non-contextual combinatorial EXP3 path choice), Li 2017/2018 and Zhao 2023 (relay selection), Busacca 2025 (link level). |
| **N2** | **Narrowed.** Bitmap receipts are prior art (CARP), and so is requesting feedback when uncertain (label-efficient prediction, Cesa-Bianchi et al. 2005; active Thompson sampling, Bouneffouf et al. 2014; entropy-gated feedback, Seraj et al. 2025). What remains: *uncertainty-gated ACK requests tied to a per-hop forwarding bandit in UWSN routing, with an audit rate and label-bias handling.* CARP and CARMA must be cited, and a CARP-style cumulative-ACK learner is included as a baseline. |
| **N3** | Intact. No prior UWSN work found that transfers a learned model between primary and backup heads. This stays **provisional** until the MDPI 2026 paper is read. |
| N-D (early receipt beacon) | **Not novel** (RFC 1122 delayed ACK, CARP). Treated as engineering and kept only as an ablation. |

---

## 3. PROPOSED: supervisor's requests of 19 Oct (v5.1, revised after the 21 Oct panel)

### P1. Max hop distance = 1 cell
- **Rule:** forward only to heads in the 26 adjacent cells, with strict progress (loop guard), passing ERGR's Hmin ≤ Hpkt rule and a predicted distance ≤ R.
- **Cell side (revised): 750 m default (4³ grid), 600 m as a sweep value.**
  - Heads sit near cell centres, so head-to-head distances at 750 m are: face 750 m, edge 1061 m, corner 1299 m, all below R = 1500 m. Corner hops have PER ≈ 0.3–0.4, and the learner and power control handle that.
  - 600 m is the strict GARP guarantee for every pair of nodes in face-adjacent cells.

| Cell side | Nodes/cell (N = 250) | Empty cells | Heads with k = 2 | Note |
|---|---|---|---|---|
| 600 m (5³) | 2.0 | 14% | 182 (73%) | Too few members: sleeping, quorums and backups all suffer |
| **750 m (4³)** | 3.9 | 2% | 120 (48%) | Default |

- **Cost to report:** one cell per hop raises the hop count compared with an ungridded node graph, so delay is reported as a trade-off, not a gain.

### P2. Two heads per cell
- k = 2, with k_eff = min(2, nodes in cell). The beacon carries `headRank`.
- Both heads are active forwarding arms **for every method**. No new learner feature.
- Report: load share between heads 1 and 2, Jain index, head-duty energy for k = 1 vs 2, and the fraction of cells with k_eff = 1.

### P3. Election (rank-slotted; replaces the sqrt(T)/2 timer)
- The original timer would elect too many heads ≈ 99.9% of the time at start-up (equal batteries) and 19–23% of the time later on.
- **Score:** s = β·E/E0 + (1−β)·(1 − dc/dmax), β = 0.9 (to confirm, Q2). At start-up β = 0.
- **Procedure:**
  1. Each node ranks itself from the beacons it has heard.
  2. Only nodes ranked ≤ k claim, by sending a Hello in slot (rank−1)·1.0 s + jitter < 0.1 s.
  3. Step-down rule: a head that hears ≥ k better heads in the same epoch sends RETIRE.
  4. Membership has a 20 m hysteresis margin and is frozen during an election.
  5. Periodic elections every 400 s are offset by cell id.
- **Simulated conflicts at 600 m:** 0% with no losses, 17–19% with 10% Hello loss, 30–37% with 20% loss. Step-down resolves them. *Rerun at 750 m*: the in-cell propagation of 0.87 s is still under the 1 s slot.
- **Planned handover** (energy < 10 J, the 400 s tick, or the head leaving its cell): the outgoing head names its successor and sends (θ, A) in fragments. The successor sends a short HEAD-ANNOUNCE. Fallback timeout ≈ 3 s, with ≤ 2 retries. **Could tier.**

### P4. Failure detection

| Evidence about head j | Reaction |
|---|---|
| ≥ 2 consecutive ACK timeouts to j with no beacon or ACK from j in between | **Suspect** for one beacon interval. Excluded from forwarding, pending attempts retried (they count toward MaxTx). Cleared by any beacon or ACK from j. |
| Beacon overdue past j's **advertised** next-beacon time + guard (beacons advertise their own interval because of N-C), confirmed by a **quorum of any 2 of {co-head, members at their next wake-up, upstream heads with timeout evidence}** | **Dead.** Election to restore k heads. |

- Beacon loss is correlated over time by shadowing (τ = 120 s), so false-alarm rates are computed in simulation, not as p².
- At 750 m there are about 120 heads.

### P5. Recovery
Three numbers are reported separately:

| Metric | Expected | Claim |
|---|---|---|
| (a) Reroute: first send after failure → delivery via the backup head | ≈ 5–6 s (ACK timeout ≈ 2.4 s at 600 m; recompute at 750 m with one-way propagation ≈ 0.87–1.0 s) | **< 10 s** |
| (b) Detection latency | ≈ 1/λ_in (≈ 14 s for one source at 0.07 pkt/s) | Reported |
| (c) Restoring k heads | Dead detection + election ≈ 2 beacon intervals + a few seconds (longer if N-C stretches the interval) | Reported |

Failure injection kills a random head in a cell with ≥ 2 nodes, at steady state, M times per run. Runs that never recover are treated as right-censored and summarised with a Kaplan–Meier median. Results are aggregated per seed, then CIs are computed.

### P6. Start-up
1. Each node sends one beacon at a random phase.
2. Rank-slotted election.
3. Hmin wave: each node's delay is proportional to its Hmin, with steps > 1 s. A node rebroadcasts only when its Hmin improves, and members do not relay.
4. Data starts.

There are 4 sinks, one at each quarter point of the surface. Start-up metrics are reported separately from the steady-state window:
- control frames until convergence (convergence = last Hmin or head change, followed by 200 s with no further change);
- time to convergence.

### P7. Learner warm start (could)
- θ0 is fitted on training seeds only. The prior precision is a fixed fraction (0.1) of the Laplace Hessian, never tuned to the ACK gate.
- **N2 claims and RQ4 (online vs offline-fitted rule) use the cold start.**
- CB-warm − A1 is reported as "value of online refinement".

### P8. Learner bookkeeping (no new features)

| Rule | Reason |
|---|---|
| ACK timeout gives label 0 at once. Attempts pending when j is declared dead are dropped from the learner but counted in P5(b). | Avoids labels that go missing in a biased way |
| Covariance inflation only on a death or an unplanned head loss | Stops a suspect state from widening every arm |
| Planned handover passes (θ, A) unscaled. The 0.7 scaling applies only to an unplanned takeover. | Avoids forgetting too much every 400 s |
| Sharing between the two heads uses increments: A = A_own + Σ A_src, replacing each source's previous contribution | Avoids double-counting, which would make the learner overconfident and request too few ACKs |
| Entry age is tracked per node and reset when the node becomes head | Keeps the feature meaningful |
| QL and linear Q also hand over their table/weights (with an ablation that turns this off) | Fair comparison of head-loss recovery |
| Discounting uses **decision** time, not label-arrival time | Keeps variable label delay (N-C, N-D) from biasing forgetting |

### P9. Evaluation frame
- **P0 = published ERGR-EMHC settings, unchanged:** original cell side and neighbour set, k = 1, original election, no v5.2 items. Extras run on P0 are labelled as such.
- The improved substrate is called **ERGR-EMHC-g** in every table.
- **Methods:** RR, **RR-P** (RR with a minimum-power rule: lowest level with predicted margin ≥ 0), QL-ERGR, CARP-style learner (cumulative/bitmap feedback, EWMA), linear Q, A1 (offline-fitted logistic), GeoOracle, CB.
- **N1 is reported on decisions with ≥ 2 viable arms** (predicted margin > 0), alongside the share of such decisions and the gap Δp between the top two arms.
- **Statistics:** common random numbers, paired bootstrap CIs, Holm correction, 20 seeds, and a 5-seed pilot to check CI width and run time.

---

## 4. NEW: v5.2 changes (grid kept), revised after the panel

### 4.1 Energy calibration comes first (finding, not a change)
The spec's provisional parameters are: 300 J, Rx 0.75 W, Tx 2 W, idle 8 mW, 5 kbit/s, 0.1 s preamble, every node beaconing every 50 s. With these, about 28% of nodes hear any given frame.

| Load | Network channel load | Heard per node | Approx. lifetime |
|---|---|---|---|
| Beacons only | 0.9 Erl | 0.25 Erl | ≈ 0.5 h |
| 0.004 pkt/s/node | 2.4 Erl | — | ≈ 0.2 h |
| 0.07 pkt/s/node | 27 Erl (pure ALOHA delivers almost nothing) | — | ≈ 0.1 h |

**Diagnosis.** At 0.75 W, a 300 J battery allows only 400 s of receiving in total. No protocol change reaches a 20,000 s run. If E0 = 10,000 J (to verify), lifetimes become 3–7 h, which is the useful range where nodes die during the run.

**Calibration, weeks 1–2:**
- **Set-up:** published parameters, 3 loads, 2,000 s runs.
- **Log:** Tx/Rx/idle energy, receive time, whether Aqua-Sim charges Rx for frames below RXThresh or for colliding frames (read the energy model code), and load per hearing range.
- **Decision rules:**
  - If the original E0, load or Rx power differ from the spec, use the published values for P0. For ERGR-EMHC-g scenarios, set E0 so that the first node dies at 30–60% of the run.
  - If Rx is > 50% of energy, N-A and N-C are **must**.
  - If Rx is < 20%, N-A, N-B and N-C drop to **could**.
  - If load is > 1 Erl per hearing range at the published rate, settle the load definition with the supervisor before week 3 (Q4).

### 4.2 Changes

| # | Change | Final form after review | Tier | Prior art |
|---|---|---|---|---|
| **N-A** | Header-first receive | Unicast DATA only. A non-addressee charges Rx for preamble + header (0.13 s instead of 0.37 s, about −64%) when the header SINR is OK. The full frame still counts as interference; if the header collided, the full frame is charged. The AR bit sits in the header. Beacons and ACKs are always fully received. Value **medium–high**: beacons dominate at low load. Presented as a modelling assumption, with the caveat that the modem must decode the header separately. | Must if calibration says Rx-dominated | Related: S-MAC (overhearing avoidance) |
| **N-B** | Members sleep | **Fixed per-cell beacon phase** with a ±2 s guard. Head beacons carry the next beacon time and any pending election. Members wake for head beacons, their own data and election epochs. **Elections after a death wait for members' next wake-up**, announced by the surviving head. A member that changes cell stays awake for one interval. **Members hand their own packets to their own head, and the head makes the bandit decision.** Only heads learn; the extra short hop is measured. Helps 52% of nodes at 750 m (only 27% at 600 m). | Should | GAF; UWAN-MAC |
| **N-C** | Lean beacons | Heads beacon every interval I; members beacon only before elections. I adapts Trickle-style **only on quantised state changes** (head, Hmin, topology), never on model changes. I_max = 2–4× base. Each beacon advertises its own next time (used by P4). Entry age stays a feature; position extrapolation uses age/I. **N3 results are reported at fixed I.** | Must if Rx-dominated | Trickle |
| **N-D** | Early receipt | A short unicast RECEIPT to the upstream sender when ≥ m receipts are pending or the oldest is > T_lab (rate-limited, does not reset Trickle). **It covers only sequence numbers sent before (now − max one-way delay − airtime)**, so packets still in flight are never marked 0. The ACK gate treats labels expected within T_lab as pending. **Engineering, not part of N2.** Optional learner-driven variant: 2-bit AR {none, expedite-receipt, ACK}, *could* tier, which would be a real N2 extension. | Should (ablation) | RFC 1122 delayed ACK; CARP |
| **N-F** | Void marking | Mark a cell as a void only when it is **dead** (not merely suspect). Mask with a TTL of 2 intervals, refreshed by the void cell, with hold-down and bounded Hmin. Applies to all methods. Any claim that "CB learns to avoid voids" is made only with N-F off. | Could | VA-GMPR; GEDAR |
| **N-G** | Bundling | **The frame is the decision unit**: one (forwarder, power) choice and one label per frame. Bundle size is capped. Only packets already queued are bundled. Per-packet CRC. Rarely triggers at low load; long frames fail more often at high PER. | Could | CARP packet trains |
| N-E (dropped) | Sink-facing heads | **Dropped.** If every head shifts toward its cell's upper face, head-to-head distances do not change. Only member→head and top-layer→sink hops change. | — | — |
| N-H/I/J (dropped) | Learned hop budget, resizing cells, grid TDMA | **Dropped/deferred**: dilutes N1, may overlap MDPI 2026, or is not at the network layer. | — | — |

---

## 5. What changes vs ERGR-EMHC (grid kept)

| Layer | ERGR-EMHC | ERGR-EMHC-g (all methods) | CB only |
|---|---|---|---|
| Cells | Published size | 750 m, 1 cell per hop, distance check | — |
| Heads | 1 | 2, both active | — |
| Election | Energy + centre | Rank-slotted, step-down, offset schedule (+ planned handover, could) | — |
| Failure handling | — | Suspect/dead tiers, quorum, backup reroute | — |
| Start-up | — | Ordered phases, Hmin wave | Warm start (could) |
| Beacons | Periodic, all nodes | Heads only, adaptive, self-advertised timing | Model blob on beacons (N3) |
| Sleep / receive | Always on | Members sleep; header-first receive | — |
| Voids | NACK | + void marking (could) | — |
| Forwarder choice | RR | RR / RR-P / baselines | Bandit (N1) |
| Power | Full | Full (RR), min-margin (RR-P) | Learned per packet (N1) |
| Feedback | NACK | Bitmap receipts, early receipt, forced ACK on overdue heads | Uncertainty-gated ACK (N2) |
| Knowledge | — | Table handover for learned baselines | Cell sharing + head handover (N3) |

---

## 6. Plan (22 weeks, tiered with gates)

Estimated effort for everything is ~30–34 person-weeks, so tiers are required.

| Weeks | Tier | Work | Gate |
|---|---|---|---|
| 1–2 | Must | Build ns-3 + Aqua-Sim NG; energy/load calibration (§4.1); time 5 pilot runs | **G0:** energy decision rule applied; load definition settled |
| 3–6 | Must | P0 reimplementation (RR) and validation against published trends | **G1:** trends match within an agreed tolerance, otherwise freeze as "our reimplementation" and tell the supervisor |
| 5–9 | Must | P1, P2/P3 (rank-slotted), P4/P5 + failure injection; N-A and N-C if G0 requires them | **G2 (wk 9):** engineering substrate frozen |
| **8–13** | Must | **CB (N1) + power + energy price; N2 gate; N3 + P8**; RR-P, QL, GeoOracle, A1 | **G3 (wk 13):** CB stable; pilot CI width OK; no new features after this point |
| 10–14 | Should | N-B sleep, N-D, P6 start-up, CARP-style learner, linear Q, 600 m sweep | Dropped if G3 slips |
| — | Could | P7 warm start, planned handover, N-F, N-G, expedite-tier AR, face-only, N sweep | Only if ahead at week 13 |
| 14–17 | — | Runs and analysis | |
| 16–22 | — | Writing | |

**Critical path:** P0 validated → calibration decision → k = 2 + election + detection → CB + N2 + N3 → pilot → runs → writing.

---

## 7. Experiments (~1,760 runs, 20 seeds, common random numbers)

| Block | Design | Runs | Answers |
|---|---|---|---|
| A: P0 published | RR, RR-P, CB, QL, A1, GeoOracle | 120 | Is CB better on the unmodified protocol? |
| B: Core (ERGR-EMHC-g, 750 m) | 8 methods × 3 loads | 480 | Main results |
| C: Substrate × learner 2×2 | engineering bundle {off, on} × {RR, CB} × 3 loads | 120 | Separates the engineering gain from the learning gain |
| C′: Leave one out | N-A, N-B, N-C each off; RR and CB; medium load | 120 | Which engineering item matters |
| D: N2 | {always-ACK, bitmap-only, gated} × {N-D off/on}, CB | 120 | ACKs per delivered packet, label delay, regret proxy (forced ACKs counted separately) |
| E: CB ablations | no sharing, no power, no energy price, greedy (no TS), warm start | 100 | N1/N3 attribution |
| F: One-factor sweeps | 600 m, N ∈ {150, 400}, k = 1, face-only × {RR, CB, QL, GeoOracle} | 400 | Robustness |
| G: Failure injection | k ∈ {1, 2} × {RR, CB, CB without sharing, QL, QL without handover} | 200 | P5 recovery, N3 |
| Pilot | | ~100 | CI width, run time |

If one run takes > 30 min, use the cluster or cut to 10 seeds where the pilot's CIs allow.

---

## 8. Metrics
- PDR, end-to-end delay (mean, p95), energy per delivered packet (Tx/Rx/idle), first node dead / half nodes dead, Jain index.
- Control frames and bytes per delivered packet, by class (beacon, receipt, ACK, election, handover, model blob).
- Learner: regret vs oracle, decisions with ≥ 2 viable arms, Δp between the top two arms, label delay by source, false-negative rate.
- ACKs split into gated / audit / forced.
- Heads: load share, election and step-down rates, P(> k heads).
- Recovery P5 (a)(b)(c); start-up metrics (P6).

---

## 9. Top risks

| Risk | Mitigation |
|---|---|
| Energy/load parameters make every method die or the channel saturate | Calibration at G0; set E0 and load with the supervisor |
| Engineering gains get credited to learning | 2×2 block C, RR-P, the ≥ 2-arm filter, P0 kept unchanged |
| The MDPI 2026 virtual-cell paper overlaps P1–P3 or N3 | Read it before G2; P1–P3 are already cited as adopted, not claimed |
| Schedule | Tiers and gates; the learner by week 8; drop the could tier first |
| Aqua-Sim PHY can't do header-first receive or per-packet power | Two-node tests in weeks 1–2 (spec §4.2); otherwise present it as a modelling assumption |

---

## 10. Questions for the supervisor (this week)

1. ERGR-EMHC's cell side and G1/G2 definition (needed for P0).
2. Meaning of β = 0.9. Is "New Election 400 s" periodic?
3. 750 m with 1 cell per hop and a distance check, or the strict 600 m GARP guarantee?
4. **Traffic definition** (0.07 pkt/s per node?), bit rate, Rx/Tx power, **E0**, and whether the original simulation charged Rx energy for every frame heard.
5. k = 2 fixed or configurable?
6. Are member sleep and header-first receive acceptable as network-layer changes?
7. Do you know the MDPI *Network* 2026 virtual-cell paper or its authors?

---

## 11. Review history

| Date | Panel | Main outcomes |
|---|---|---|
| 19 Oct | R1 protocol, R2 bandits, R3 novelty, R4 evaluation (on v5.1) | Rank-slotted election; ≥ 2-timeout suspect; < 10 s only for rerouting; P0 scenario; cold start for N2; incremental sharing; label rules |
| 21 Oct | Same four roles (on v5.2) | E0/load is the root cause of the energy issue; N-E dropped; 750 m confirmed using head-to-head distances; N-D is engineering (RFC 1122, CARP); N2 narrowed, CARP baseline added; N-B redesigned (fixed phases, member → own head); N-G uses per-frame decisions; quorum redefined; RR-P and the 2×2 block added; tiered plan with gates |
