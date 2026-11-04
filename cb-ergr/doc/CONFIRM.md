# CONFIRM.md — unverified items and the defaults used

These are items the spec marks **[CONFIRM]** or lists as unknown (§5.2), plus
facts found while reading the code that need a decision from the thesis
author or supervisor. Each one is (or will be) an ns-3 Attribute with the
default shown. The "Used in" column is filled in as each milestone lands
(`—` means not implemented yet).

Status key: **open** = not verified; **verified** = checked against a source
(the source is named); **changed** = default differs from the spec, with the
reason.

## A. Base protocol (ERGR-EMHC, Al-Salti et al. 2019)

| # | Item | Default used | Attribute | Used in | Status |
|---|---|---|---|---|---|
| A1 | Cell edge length (**ERGR-EMHC-g now 750 m per kickoff §2.1 / D§3 P1; the note below applies to P0 only**) | R/√3 ≈ 866 m (R = 1500 m). **Note:** for two points in *face*-adjacent cubes the worst-case distance is √(2²+1+1)·a = √6·a, which equals R when a = R/√6 ≈ 612 m. If edge- and corner-adjacent cells also count as neighbours (26-neighbourhood), the worst case is √12·a, so a = R/(2√3) ≈ 433 m. The value a = R/√3 only guarantees range within the *same* cell (cell diagonal √3·a = R), not to adjacent cells. Needs a decision: cell size, and 6- vs 26-neighbourhood. | `CellSizeM` | — | open |
| A2 | Head-election criterion | Highest residual energy, then closest to the cell centre, then lowest id | `ElectionCriterion` | — | open |
| A3 | Election triggers and messages | Head leaves its cell, head dies, or head energy < `HeadEnergyThreshold` | `HeadEnergyThreshold` | — | open |
| A4 | Hmin dissemination | Sinks advertise Hmin = 0; each node sets Hmin = 1 + min Hmin over G1 heads heard; carried in beacons | — | — | open |
| A5 | Hpkt at the source | Hpkt = Hmin(source), decremented each hop | — | — | open |
| A6 | Void handling / NACK | No eligible forwarder → NACK to previous hop, which retries with Hpkt + 1 up to `VoidRetryLimit` | `VoidRetryLimit` | — | open |
| A7 | G1 contains only neighbouring cell-heads | Yes | — | — | open |
| A8 | Periodic beacons and period | Yes, `BeaconPeriodS` = 50 s, uniform random phase | `BeaconPeriodS` | — | open |
| A9 | ERGR-EMHC already uses hop ACKs | No (RR baseline sends no hop ACKs) | — | — | open |
| A10 | Beacons already carry residual energy | No — CB adds 1 byte, counted as overhead | — | — | open |
| A11 | MAC in the original evaluation | Aqua-Sim NG broadcast/ALOHA MAC, MAC-level ACKs disabled | `MacType` | — | open |
| A12 | Scenario | 3×3×3 km box; R = 1500 m; 150 B payload; 0.07 pkt/s per node; 4 surface sinks at the quarter points; N = 250 | scenario args | — | open |
| A13 | Which ERGR message carries the handover blob (§6.4) | Dedicated HANDOVER unicast to the new head, counted as control overhead | — | — | open |

## B. Energy (§4.3) — confirm against the group's previous ERGR-EMHC settings

| # | Item | Default used | Attribute | Used in | Status |
|---|---|---|---|---|---|
| B1 | Initial energy | 300 J | `InitialEnergyJ` | — | open |
| B2 | Full transmit power | 2.0 W | `TxPowerFullW` | — | open |
| B3 | Power-level scales | {0.25, 0.5, 1.0} | `TxPowerLevelScale` | — | open |
| B4 | Source-level offsets | {−6, −3, 0} dB | `SourceLevelOffsetsDb` | — | open |
| B5 | Receive power | 0.75 W | `RxPowerW` | — | open |
| B6 | Idle power | 0.008 W | `IdlePowerW` | — | open |
| B7 | Target lifetime for the energy price | 1e5 s (per scenario) | `TargetLifetimeS` | — | open |

Upstream `AquaSimEnergyModel` defaults (verified in `model/aqua-sim-energy-model.cc`,
2026-10-15): TxPower 0.660 W, RxPower 0.395 W, IdlePower 0.008 W,
InitialEnergy 10000 J. The Attribute help text says "(1.6W)" / "(1.2W)",
which does not match the values. The scenario will set every value
explicitly.

## C. Baselines and evaluation

| # | Item | Default used | Attribute | Used in | Status |
|---|---|---|---|---|---|
| C1 | B2 QLearning definition vs. the group's internal QL-ERGR-EMHC proposal | QELAR-style: αq = 0.3, γq = 0.8, ε = 0.1, r = success − 0.1, V_j advertised in beacons (+2 B), Q init = 1 + Hmin heuristic, AckMode = Every | selector attrs | — | open |
| C2 | Per-node traffic rate | 0.07 pkt/s (the pilot used 0.004) | `PktRate` | — | open |
| C3 | B1 regression reference numbers (test 16) | Qualitative only: PDR falls as speed rises | — | — | open |

## D. Found while reading the code (M0)

| # | Item | Finding | Decision needed |
|---|---|---|---|
| D1 | Reference pilot `cb_ergr_pilot.py` | **Not supplied** with the spec (only the spec file was uploaded). M1 ports the link model and current field from it, and M1's acceptance compares PER curves against it. | Please upload `cb_ergr_pilot.py`. Without it, M1 can follow the formulas in §4.4–4.6, but `calibrate_sl`, the exact Stojanovic constants, the `current()` field and the OU parameters cannot be matched to the pilot. |
| D2 | Reception is not SNR-based upstream | Verified: `AquaSimRangePropagation::ReceivedCopies` gives a copy to every node within `TxRange` (−1 = unlimited); `AquaSimSignalCache::UpdatePacketStatus` uses the *receiver's energy-model RxPower* as "signal strength" for SINR, not the received power; `AquaSimThresholdSinrChecker` threshold defaults to 0. So success does not depend on SNR or transmit power. §4.4 must be implemented (planned as a propagation subclass + PHY reception hook, behind an Attribute). | None — this confirms the spec's warning. |
| D3 | Effective preamble | Verified: constructor sets `m_preamble = 1.5`, then Attribute `Preamble` (default 0) overwrites it at construction, so the effective preamble is 0. `CalcTxTime` uses `Time::FromInteger(Preamble(), Time::S)`, which truncates. | None — patch §4.1 planned for M1. |
| D4 | Default modulation bit rate | `AquaSimModulation` default is SPS = 10000, CodingEff = 1 → 10 kbit/s. The spec uses `BitRate` = 5000 bit/s. | None — the scenario will set SPS/CodingEff explicitly. |
| D5 | ns-3 version | Built against ns-3.41 from the official `ns-3.41` git tag (the `ns-allinone-3.41.tar.bz2` download is blocked in this environment; the source tree is the same). | None. |

## E. Added 2026-11-04 (kickoff + design v5.2)

| # | Item | Default used | Attribute | Used in | Status |
|---|---|---|---|---|---|
| E1 | ERGR-EMHC-g cell side | 750 m (4^3 cells); 600 m sweep | `CellSizeM` | params::grid | decided by kickoff; P0 value still A1 |
| E2 | Heads per cell, neighbour set | k = 2; 26 adjacent cells, `FaceOnly` false | `HeadsPerCell`, `FaceOnly` | params::grid | decided by kickoff |
| E3 | Election weight beta, periodic election | beta = 0.9, 400 s | `ElectionBeta`, `ElectionPeriodS` | params::election | open (Q2) |
| E4 | Forgetting clock | decision time, not label arrival | — | `CbLearnerModel::BeginDecision` | decided (K§2.3) |
| E5 | What sharing sends | diag of own evidence (A_own - lambda0 I) on beacons; full A only on planned handover | — | `CbLearnerModel` | assumption: spec §6.3 blob has diag only |
| E6 | Does Aqua-Sim charge Rx for frames below RXThresh or colliding? | **Yes (read in D2).** `PrevalidateIncomingPkt` calls `UpdateRxEnergy` for every frame that reaches it (only failed nodes and frequency mismatches return earlier). If the node is already receiving, only the extra overlap is charged, and only for error-flagged frames. Upstream charges for the propagation delay, not the airtime (patch P2 fixes this). | — | PHY patches | verified in code |
| E7 | Energy model defaults | Constructor sets 0.75 W / 2.0 W, but the registered Attribute defaults (0.395 W Rx, 0.660 W Tx, 10000 J) overwrite them at creation. `CbErgrHelper::ConfigureHelper` always sets all four values explicitly. | `InitialEnergy`, `TxPower`, `RxPower`, `IdlePower` | helper | verified in code |
| E8 | Pilot constants | Stojanovic noise uses the standard 4-term model at shipping 0.5, wind 5 m/s; calibrated full-power SL = 142.5 dB; margin at R = -1.1 dB. These come from the spec formulas, **not** from the pilot (not supplied). | `ShippingActivity`, `WindSpeedMps` | `AcousticLinkModel` | open until the pilot is compared |
| E9 | Frame length used for PER | The size in the Aqua-Sim header (`AquaSimHeader::GetSize`), i.e. what the upper layers set. Calibration uses 150 B. | `CalibFrameBytes` | `CbErgrPropagation` | assumption |
| E10 | Ocean-current field and OU parameters | Stand-in meander: vx = U cos(2 pi y/L + phi(t)), vy = 0.5 U sin(2 pi x/L + phi(t)), phi = 0.5 sin(2 pi t/T), T = 3600 s; OU tau 300 s, sigma 0.3 U per axis; vertical drift 0.1 U sin(2 pi t/T + random phase). The pilot's `current()` was not supplied. | `CurrentSpeed`, `OuTauS`, `OuSigmaFraction`, `MeanderPeriodS` | `CurrentDriftMobility` | open until the pilot is compared |
| E11 | Eligibility details | Sinks eligible from anywhere in range; strict progress = smaller Hmin or shallower; previous hop excluded; members hand packets to own-cell heads and the head restarts Hpkt from its own Hmin | `StrictProgress` | `ErgrEmhcRouting::GetEligible` | assumption (K§2.1 does not define "strict progress") |
| E12 | Election details | First round 55 s + 2 s x cell id, beta 0 then 0.9, period 400 s, triggers debounced to one per beacon period, claims unconditional with retire-on-conflict | `FirstElectionS`, `ElectionOffsetPerCellS`, `ElectionBeta`, `ElectionPeriodS` | `ErgrEmhcRouting` | open (Q2) |
| E13 | Load and battery (calibration, D§4.1) | At N = 250 with the published 300 J, Rx is ~90% of energy and the first node dies after 670 s (0.07 pkt/s), 1036 s (0.004) or 1287 s (no data). At 0.07 pkt/s the channel is saturated (round-robin PDR 3.5%). | `InitialEnergyJ`, `PktRatePerNodeHz` | scenario | **needs supervisor (Q4)**: E0, load definition |
| E14 | Frame loss accounting | PER losses are exact per frame type; collisions + half-duplex are one number per node (PHY error events minus PER losses) | — | `MetricsCollector` | limitation |
| E15 | Spec test 16 (PDR falls with speed) | **Not reproduced.** Round robin PDR at N = 100, 0.004 pkt/s, training seeds 101-103: 0.272 (U=0), 0.287 (0.5), 0.294 (1), 0.294 (2), flat within noise. Mobility does show in elections/hour (116 -> 486), head tenure (1242 s -> 350 s) and voids (7 -> 24 per run). Nothing was tuned to change this. | — | — | open: published reference numbers (C3) needed |
| E16 | Per-hop channel quality | With sigma = 5 dB shadowing and R = 1500 m calibrated to PER 0.5, head-to-head hops at 750 m cells have PER 0.07-0.4, so round robin without retransmissions delivers 15-30% at N = 100. This is the effect the learner is meant to exploit. | `ShadowSigmaDb` | — | observation |
| E17 | PHY receive delay | Aqua-Sim delivers a frame to the MAC 2 airtimes after it starts arriving (`PrevalidateIncomingPkt` schedules `CollisionCheck` after one airtime, then `PktSubmissionTimer` waits another). Measured; the ACK timeout allows for it. | `PhyRxDelayAirtimes` (1) | ACK timeout | verified in code and by label statistics |
| E18 | Hop reliability in feedback mode | Member hand-offs and head forwards both get ACK + retries (up to MaxTx = 3); only head decisions are learned | `MaxTx`, `RetriesAlwaysAck` | `ErgrEmhcRouting::SendData` | assumption |
| E19 | Channel load with retries | RR+FB at N = 100, 0.004 pkt/s: 5.6 attempts per packet, ground-truth hop success 0.35, PDR 0.44 (RR without feedback: 0.27-0.29). 17% of timed-out attempts had actually been received (lost ACK). Collisions dominate over PER at this load because beacons are ~28% of the airtime. | — | — | observation (see D§4.1) |
| E20 | Effect of E0 on the baseline | With E0 = 10000 J instead of 300 J, RR PDR at N = 100, 0.004 pkt/s is 0.29-0.31 (E0 300 J gave 0.27 over 2500 s because nodes start to die after ~2000 s). | `InitialEnergyJ` | — | observation |
| E21 | ACK gate never relaxes in 2000 s runs | AR rate stays 1.0 at 0.004 and 0.02 pkt/s: width(x) = sqrt(x^T A^-1 x) is about 1 initially and needs roughly 25 effective samples per head to fall below TauW = 0.3; with the 2060 s half-life each head sees too few decisions. | `TauW`, `Gamma` | `SelectorCbV5::NeedsAck` | open: re-check at N = 250, 20000 s, and with `Gamma` sweeps; not tuned |
| E22 | FW weights policy and the "value of learning" | FW weights are hand-set by physical reasoning; FW >= CB in these short runs. Needs a decision: keep hand-set, use the A1 offline fit, or both as separate baselines. | `params::selector::kFwWeights` | `SelectorFixedWeight` | open, important for the N1 claim |
| E23 | Failure detection | Suspect after 2 consecutive ACK timeouts (skipped for one beacon period, last-resort fallback); dead = 3-beacon expiry of a head row, no quorum of 2 | `SuspectTimeouts`, `NeighbourTimeoutBeacons` | routing | simplification of D§3 P4 |
| E24 | Handover details | Successor = best-scored non-head member; model adopted if it becomes head within 400 s; unplanned takeover uses the last shared blob x 0.7; A off-diagonals sent as int16 | `HandoverKeepS`, `BetaHandover` | `SelectorCbV5` | assumption |
