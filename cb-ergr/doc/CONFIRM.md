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
