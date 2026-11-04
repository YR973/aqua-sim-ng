# CB-ERGR: Claude Code kickoff (implementation, day 1)

Author: Yousef Roudani (YR), MSc thesis, Dept. of Computer Science, Sultan Qaboos University. Supervisor: Prof. Abderezak Touzene.
Written 5 Nov 2026. Hand this file to Claude Code as the first message (or save it as `CLAUDE.md` at the repo root).

---

## 0. Read this first (instructions to Claude Code)

You are implementing the simulation for an MSc thesis: **CB-ERGR**, a contextual-bandit per-hop forwarder added to the grid-based underwater routing protocol **ERGR-EMHC**, in **ns-3 + Aqua-Sim NG**.

**Source-of-truth documents** (YR will put them in `docs/` before you start; if they are missing, stop and ask):

| File | Role |
|---|---|
| `docs/cb-ergr-v5-implementation-spec.md` | Full spec of v5 (15 Oct): module layout, headers, algorithms, attributes, tests, milestones M0–M10. **Base reference for formulas, headers, timers and tests.** |
| `docs/cb-ergr-design-final-detailed.md` | Design v5.2 (21 Oct). **Wins over the spec where they disagree.** |
| `docs/cb_ergr_pilot.py` (if present) | Toy Python model: link model (Thorp, Stojanovic noise, BFSK PER, Gauss–Markov shadowing) and the current field. Not a protocol reference. |
| `docs/CODE-GUIDE.md` | YR's reading/modifying guide for the code. Copy it to `doc/` and keep it up to date (rule 9). |
| This file | The delta from spec v5 to what is built now (§2), plus the literature-review refinements (§3) and today's plan (§4). **Wins over both docs above.** |

### 0.1 The old v5 spec: what still counts and what to ignore
`docs/cb-ergr-v5-implementation-spec.md` was written on 15 Oct as a standalone brief for a coding agent. It is **superseded as a plan** and kept **only as a technical reference**. Do not follow it on its own.

| Part of the old spec | Status |
|---|---|
| §0 "Instructions for the coding agent" (stop for review after every milestone) | **Ignore.** Use the working rules below. Keep only its CONFIRM.md/PATCHES.md, determinism and style rules, which are repeated below. |
| §15 milestones M0–M10 and their order | **Ignore.** Use the plan in §4 of this file (D0–D7). |
| §11 baseline list (B1–B4, A1, CB-lin `DLinUcb`, P-ladder) | **Ignore.** Use §2.4 of this file. `DLinUcb` (CB-lin) is **dropped**; do not build it. |
| §12 sweep plan and §13 metrics list | **Partly superseded.** §2.5 and §2.6 of this file come first; use the spec's lists only for anything they don't cover. |
| Defaults that conflict with §2 (cell size R/√3, one head per cell, power on by default, ACK timeout leaving the label pending, per-node models, inflation on every known change) | **Ignore.** Use §2. |
| §4 PHY/channel/energy/mobility models, §6 headers, §7 state, §8 algorithms, §9 event handling, §10 attribute names, §14 tests, §16 "things not to do" | **Use**, except where §2 of this file changes them. |

Other files from the old work (if YR copies them in):
- `cb_ergr_pilot.py`: use **only** its link model and current field (Thorp absorption, noise, BFSK PER, shadowing, ocean current). Its protocol logic and numbers are an early toy model, not a reference.
- `cb-ergr-v5-research-notes.md`, `pilot_raw_results.jsonl`, `pilot_summary.csv`, the v4/v4.1 proposals: **do not read or use**; they are older and conflict with v5.2.
- Any older code (e.g. a `ThesisCode` folder): do not build on it unless YR says so; start the module fresh as described here.

When the old spec and this file disagree and §2 doesn't settle it, follow this file, then the design doc, then the spec, and log the decision in `CHANGELOG.md`.

**Working rules for today (override the old spec's §0):**
1. YR has a **one-day budget**. Do not stop for review after each milestone. Work through §4 in order and only stop at the gates marked **STOP**. Keep going otherwise.
2. After each milestone: run its tests, `git commit` with a clear message, and append 3–6 lines to `PROGRESS.md` (what was done, test results, what is next, open problems). The next session must be able to resume from `PROGRESS.md` alone.
3. Every unverified fact about ERGR-EMHC or the scenario is an ns-3 **Attribute** with the default given here, and gets a row in `doc/CONFIRM.md` (item, default, where used). Never guess silently.
4. Every upstream Aqua-Sim NG change is minimal, guarded by an Attribute (upstream behaviour by default), and listed in `doc/PATCHES.md`.
5. Determinism: all randomness through ns-3 `RandomVariableStream` + `AssignStreams()`. Same seed and run → identical CSV.
6. C++17, ns-3 style, no external linear-algebra library. Python 3 for scripts (numpy, pandas, matplotlib, scikit-learn allowed).
7. The learner must never read ground truth, true positions or true shadowing. Only the oracles may.
8. **Log every change and the reason for it** in `doc/CHANGELOG.md`, newest first, one entry per change (not per commit), in this form:
   ```
   ## 2026-11-05 14:20 — <short title>
   - What: <files touched, what changed>
   - Why: <the reason: spec section, test failure, design delta in §2, ambiguity resolved, bug found>
   - Effect on results: <none | which metric or behaviour may change>
   - Spec/design deviation: <no | yes: which section, and the CONFIRM.md row if any>
   ```
   This includes deviations from the docs, upstream patches (also in `PATCHES.md`), default values you picked, bugs you fixed and things you tried that failed. YR has to defend every choice in the thesis, so "why" matters more than "what". Keep entries short and plain. Commit messages should repeat the title of the entry.
9. **Keep `doc/CODE-GUIDE.md` true.** YR wrote a first version (it is in `docs/CODE-GUIDE.md`; copy it to `doc/` in D0). Whenever you add, rename or remove a file, an Attribute, a selector or an output column, update the guide in the same commit. The guide is for a reader who knows Java and some C++, not ns-3. Use the function names it promises (`GetEligible`, `BuildFeatures`, `ApplyForgetting`, `Score`, `NeedsAck`, `OnSent`, `OnLabel`, `PredictedDistance`, `WriteSummary`) and the command-line names in its §5, or change the guide to match. Put a 2–4 line comment at the top of every `.cc` file saying what it does and which spec section it implements.
10. **Readable, maintainable code comes first.** YR must read, defend and change this code himself, so readability matters more than cleverness:
    - **Comments explain why, not just what.** Every function gets a short header comment: what it does, its inputs and outputs, and the spec/design section it implements (e.g. `// Spec §8.3: decay A and theta toward the cell prior`). Comment every non-obvious line, magic number, unit (s, m, J, dB) and assumption.
    - **Simple lines over dense ones.** Split complex one-liners, nested ternaries, long chained calls and clever lambdas into several plain steps with named intermediate variables. Example:
      ```cpp
      // Avoid:
      double s = sigmoid(th.dot(x)) * (1 - kp + kp * x[1]) - lam * P[p] * at - mu * (std::pow(1 / std::max(b, .01), k) - 1);

      // Prefer:
      double pSuccess     = Sigmoid(thetaSample.Dot(x));            // predicted hop success
      double progressW    = 1.0 - m_kappaP + m_kappaP * x[kHopProgress];
      double txEnergyJ    = m_txPowerW[level] * airtimeS;           // energy this hop costs
      double batteryCost  = BatteryPenalty(batteryFraction);         // phi(e), spec §8.5
      double score        = pSuccess * progressW
                          - m_lambdaE * txEnergyJ
                          - m_mu * batteryCost;
      ```
      Make an exception only where it measurably affects performance in a hot loop (and say so in a comment). The compiler optimises named temporaries away, so this is almost never the case.
    - **Small functions** (roughly ≤ 40 lines) with one job each and descriptive names (`BuildFeatures`, `ShouldRequestAck`, not `proc2`). No function should need scrolling to understand.
    - **Named constants, not magic numbers**: `constexpr int kHopProgress = 1;` for feature indices, and every tunable value is an Attribute with a units-bearing name (`AckGuardS`, `CellSizeM`).
    - **Descriptive variable names with units** where it helps: `distanceM`, `delayS`, `energyJ`. Short names (`i`, `j`, `x`) only for loop indices and the spec's maths symbols, with a comment mapping them to the spec.
    - **No hidden behaviour:** no macros for logic, no global state, no surprising side effects in getters, no deep inheritance (one level below the base classes is enough).
    - **Consistent structure across selectors**, so learning one teaches the rest: the same function order and the same section comments (`// --- Selection ---`, `// --- Learning ---`, `// --- Sharing ---`).
    - **Tests double as documentation:** name tests after the behaviour they check (`AckTimeoutGivesLabelZero`), with a one-line comment saying which spec rule they protect.
    - When you refactor for readability, keep the behaviour identical, run the tests, and log the refactor in `CHANGELOG.md` like any other change.
11. **All parameters live in one file.** Every tunable value (cell size, transmission range, power levels, Tx/Rx/idle power, battery, bit rate, preamble, beacon period, timers, learner settings, scenario size, traffic rate, mobility, channel settings, seeds) is declared **once**, in `model/cb-ergr-params.h`, and every other file includes it. No other file may contain a numeric default for a parameter.
    - Group the values in namespaces by topic, each with a units suffix and a one-line comment giving its source and status, e.g.:
      ```cpp
      // cb-ergr-params.h: the ONLY place where parameter defaults are defined.
      // Change a default here, rebuild once, and every class and script picks it up.
      // Status tags: [SPEC] from the spec, [DESIGN] design v5.2, [CONFIRM] unverified (see CONFIRM.md), [TUNED] chosen on training seeds.
      namespace cbergr::params {

      namespace grid {
        constexpr double kCellSizeM        = 750.0;   // [DESIGN] D§3 P1; 600 m is a sweep value
        constexpr int    kHeadsPerCell     = 2;       // [DESIGN] D§3 P2
      }

      namespace phy {
        constexpr double kTxRangeM         = 1500.0;  // [SPEC] S§5.2
        constexpr double kBitRateBps       = 5000.0;  // [CONFIRM] Q4
        constexpr double kPreambleS        = 0.1;     // [SPEC] S§4.1
        constexpr int    kNumPowerLevels   = 3;       // [SPEC] S§4.3
        constexpr std::array<double, 3> kTxPowerLevelScale  = {0.25, 0.5, 1.0};
        constexpr std::array<double, 3> kSourceLevelOffsetDb = {-6.0, -3.0, 0.0};
      }

      namespace energy {
        constexpr double kInitialEnergyJ   = 300.0;   // [CONFIRM] Q4; recalibrate for ERGR-EMHC-g
        constexpr double kTxPowerFullW     = 2.0;     // [CONFIRM] Q4
        constexpr double kRxPowerW         = 0.75;    // [CONFIRM] Q4
        constexpr double kIdlePowerW       = 0.008;   // [CONFIRM] Q4
      }

      namespace learner {
        constexpr double kForgetGamma      = 0.98;    // [SPEC] S§8.3, per kForgetPeriodS
        constexpr double kForgetPeriodS    = 60.0;
        constexpr double kAckWidthThreshold = 0.30;   // [SPEC] S§8.6
        constexpr double kAuditRate        = 0.10;    // [SPEC] S§8.6
      }
      // ... scenario, mobility, channel, timers, election, sharing, price, logging
      }  // namespace cbergr::params
      ```
    - **Attributes take their defaults from this file** (`DoubleValue(params::grid::kCellSizeM)`), so the command line can still override any value for a single run without rebuilding. Constants that are not meant to be tuned (e.g. feature indices) also live here, in their own namespace.
    - Python scripts read the same values: generate `scripts/params.json` from the header at build time (or keep a test that checks the two agree), so the analysis never uses a stale value.
    - Add **run-time config files** for experiments: `configs/base.txt`, `configs/p0-published.txt`, etc., in ns-3 `ConfigStore` raw-text format (or a simple `key = value` file the scenario parses), loaded with `--ConfigFile=configs/base.txt`. A config file overrides the header defaults, and the command line overrides the config file. Each run copies the full resolved parameter set into its `OutDir` as `params_used.txt`, so every result can be traced to its exact settings.
    - Add a test or CI check that greps `model/` and `examples/` for numeric literals in `AddAttribute(...)` defaults and fails if any default is not taken from `cb-ergr-params.h`.

**Saving credits:**
- Build only what you need (`./ns3 build cb-ergr`, not the whole tree, after the first build). Use the `debug` profile for tests and `optimized` for any multi-run experiment.
- Smoke runs: `SimTimeS=2000`, N = 100. Never run the full 20,000 s × 20-seed sweeps today.
- Don't paste long logs into the conversation. `grep`/`tail` them.
- Prefer writing a test over reasoning about whether code is right.

---

## 1. What the thesis is (two-minute version)

- **ERGR-EMHC** (Al-Salti, Alzeidi, Day & Touzene, *Computer Networks* 162, 2019, doi 10.1016/j.comnet.2019.106869): 3D space divided into cubic cells. Each cell has an elected head. Each node keeps the heads of neighbouring cells (G1 table), knows its minimum hop count to a sink (**Hmin**), and each packet carries a hop budget **Hpkt**. A forwarder is picked from G1 in **round robin** among those with Hmin ≤ Hpkt. Voids are handled with NACKs. No hop-by-hop ACKs.
- **CB-ERGR** replaces round robin with a **contextual bandit**: a discounted logistic model of one-hop delivery success, with Laplace–Thompson sampling over the eligible next-cell heads (and, optionally, transmit-power levels). An online energy price keeps consumption within a budget.
- **Claims:**
  - **N1:** per-hop contextual-bandit forwarding inside a multi-hop UWSN routing protocol.
  - **N2:** ACK requests gated by the learner's own uncertainty, with an audit rate and label-bias handling. Bitmap receipts (CARP) and label-efficient prediction are prior art.
  - **N3:** the model is shared between the two heads of a cell and handed over on head change.
- Everything else (cell size, two heads, election, failure detection, lean beacons, sleep) is engineering adopted from the literature and applied to **all** methods. The substrate with these changes is called **ERGR-EMHC-g**.

---

## 2. Delta: spec v5 → what to build (v5.2 + supervisor decisions)

Apply these on top of the spec. Section numbers refer to the spec (S§) or the design doc (D§).

### 2.1 Grid and heads
| Item | Spec v5 | Build now |
|---|---|---|
| Cell side | R/√3 ≈ 866 m | **`CellSizeM` = 750 m** (4³ cells in the 3 km cube); 600 m as a sweep value. P0 (published ERGR-EMHC) keeps its own value [CONFIRM]. |
| Neighbour set | G1 heads | Heads in the **26 adjacent cells** only, with **predicted distance ≤ R** and **strict progress** (loop guard), plus Hmin ≤ Hpkt. Attribute `FaceOnly` (default false). |
| Heads per cell | 1 | **k = 2** (`HeadsPerCell`), k_eff = min(k, nodes in cell). Beacon carries `headRank`. Both heads are forwarding candidates for every method. |
| Election | Highest energy → centre → id | **Rank-slotted** (D§3 P3): score s = β·E/E0 + (1−β)(1 − dc/dmax), β = 0.9 [CONFIRM], β = 0 at start-up. Nodes rank themselves from beacons; rank ≤ k claim in slot (rank−1)·1.0 s + jitter < 0.1 s; a head hearing ≥ k better heads sends RETIRE (step-down). 20 m membership hysteresis. Periodic election every 400 s [CONFIRM], offset by cell id. Planned handover = *could* tier. |
| Failure detection | — | D§3 P4: **suspect** after ≥ 2 consecutive ACK timeouts with no beacon/ACK in between (excluded for one beacon interval); **dead** when the beacon is overdue past its advertised next time + guard, confirmed by a quorum of 2. Then re-election. |
| Members | Forward like heads | **Members hand their own packets to their own head; only heads make the forwarding decision and only heads learn** (D§4.2 N-B). The extra member→head hop is measured. |

### 2.2 Energy and channel load: calibrate first (D§4.1)
With the spec defaults (300 J, Rx 0.75 W, 50 s beacons from every node), nodes die within the first hour because of receive energy, whatever the protocol does. So:
- `InitialEnergyJ` is an Attribute (default 300 J for P0; for ERGR-EMHC-g, set later so the first node dies at 30–60% of the run).
- Log Tx/Rx/idle energy separately, plus receive time. Read Aqua-Sim's energy model and record in `CONFIRM.md` whether it charges Rx for frames below RXThresh and for colliding frames.
- Implement **N-A header-first receive** (D§4.2) and **N-C lean beacons** behind flags (`HeaderFirstRx`, `LeanBeacons`, both default false). Turn them on for ERGR-EMHC-g only if calibration shows Rx > 50% of energy.
  - N-A: a non-addressee charges Rx for preamble + header only.
  - N-C: heads beacon; members only before elections. Trickle-style interval on quantised state changes only. Every beacon advertises its next beacon time.

### 2.3 Learner (SelectorCbV5) changes
| Item | Spec v5 | Build now |
|---|---|---|
| Transmit power | Arm by default | **`EnablePower` default false** (supervisor: optional). Main results are forwarder-only; power is an ablation. Do the S§4.2 power patch only after the core works. Its gate is no longer blocking. |
| Model scope | Per node | **One model per cell, held by its heads.** Sharing between the two heads is incremental: A = A_own + Σ A_src, replacing each source's previous contribution (no double counting). |
| Handover | Always A ← 0.7·A_old | **Planned** handover passes (θ, A) unscaled; the 0.7 factor applies only to an **unplanned** takeover (head died). |
| Covariance inflation | On every known change | Only on a **death or an unplanned head loss** (D§3 P8). |
| ACK timeout | Label stays pending (S§9.4) | **ACK timeout = label 0 at once** (D§3 P8). Attempts pending when the forwarder is declared dead are dropped from the learner and counted in recovery metrics. |
| Discounting clock | Not specified | Discount by **decision time**, not label-arrival time. |
| Entry age (x5) | First heard | Tracked per node, **reset when the node becomes head**. |
| Margin (x8) | Model-based by default | **`MarginFromRssi` default true** (measured SNR of the candidate's last beacon); the model-based margin is the fallback and an ablation (review R2). |
| Features | 9 (8 + intercept) | Same 9, plus optional **x_onward** (review R1, flag `OnwardFeature`, default false → D = 10). It is the number of adjacent cells the candidate head can make strict progress to, normalised by 26, or 0 for a void; carried as 1 byte in head beacons. Make `D` a compile-time template parameter (9 or 10). |

Everything else in S§7–S§9 (features x1–x9, forgetting toward θ_cell with Γ = 0.98 per T0 = 60 s, Thompson sample, score with KappaP and energy terms, ACK gate width > 0.30 or audit 10% or retry, send-time evidence, one Newton step per label, dual price, receipts) stays as specified.

### 2.4 Baselines (D§3 P9; replaces S§11 list for today)
| ID | Selector | Note |
|---|---|---|
| RR | `RoundRobin` | ERGR-EMHC as published; no hop ACKs; full power |
| RR-P | `RoundRobinMinPower` | RR plus the lowest power level with predicted margin ≥ 0 (only meaningful with power) |
| FW | `FixedWeight` | **New (review R5):** same features, hand-set weights, argmax. Isolates the value of learning the weights. |
| QL | `QLearning` | S§11 B2 (QELAR-style), same candidates and feedback, AckMode = Every. **Review R10: must have.** |
| CARP | `CarpEwma` | S§11 B3, with cumulative/bitmap feedback |
| LinQ | `LfaQ` | S§11 B4 |
| A1 | `FittedLogistic` | offline-fitted on training seeds 101–110 only |
| GO | `GeoOracle` | S§11 GO |
| O | `Oracle` | S§11 O, for regret |
| CB | `CbV5` | the method |

Report N1 only on decisions with ≥ 2 viable candidates (predicted margin > 0), with the share of such decisions.

### 2.5 Evaluation blocks to make possible (no need to run them today)
- **P0** = published ERGR-EMHC settings unchanged (k = 1, original cell size and election, no v5.2 items).
- **2×2 block:** engineering bundle {off, on} × {RR, CB}, so the gain from engineering is not credited to learning.
- **Review R11 (ACK gate):** AckMode ∈ {Every, BitmapOnly, Gated}; sweep `TauW`. Report ACK frames per delivered packet, and calibration of the model on the **audit** sample only (the audit is the unbiased sample, because missing receipts are not missing at random).
- **Review R12 (sharing/handover):** {shared + handover, cold start per new head, prior only} with head-failure injection.
- **Review R3 (forgetting):** sweep the half-life 500–2,000 s, i.e. Γ per 60 s from 0.92 to 0.98. At Γ = 0.98 the half-life is about 2,060 s.
- **Review R13 (could):** warm-start a new cell's θ from the FW weights or the mean of neighbouring cells.
- **Review R6:** meandering-current mobility (spec S§4.6) as well as random walk.

### 2.6 Logging: every transmission, per node and per packet
The spec (S§13) only gives one summary row per run plus an optional per-attempt decisions file. That is not enough to debug the protocol or to explain results in the thesis. Build these outputs in `MetricsCollector`. Each one is a separate file in the run's `OutDir`, controlled by a flag, and written in CSV with a header row and units in column names.

| File | Flag (default) | One row per | Columns (minimum) |
|---|---|---|---|
| `summary.csv` | always | run | as S§13.1, plus totals for every counter below |
| `nodes.csv` | `LogNodes` (true) | node, at the end of the run (and every `NodeLogIntervalS` = 1000 s if `LogNodeTimeline` = true) | node, role history (time as head), cell, final/average energy, death time; **by frame type (DATA, ACK, BEACON, ELECTION, HANDOVER, NACK):** frames sent, bytes sent, frames received OK, frames lost (and why); **DATA forwarding:** packets originated, packets forwarded, first-attempt successes, successes after retry, retransmissions, drops (by reason), duplicates received, ACKs requested/sent/received, ACK timeouts; hop success rate; mean label delay; Tx/Rx/idle energy (J) |
| `links.csv` | `LogLinks` (true) | (sender, forwarder) pair that carried data | attempts, successes (ground truth), labels 1/0 by source (ACK, bitmap, expiry), retransmissions, mean distance (m), mean SNR (dB), mean predicted success, first/last use time |
| `packets.csv` | `LogPackets` (true) | DATA packet (end to end) | source, sequence number, creation time, delivered (y/n), delivery time, sink, E2E delay (s), hop count, total transmissions, total retransmissions, path (node ids separated by `-`), drop node and drop reason if lost |
| `decisions.csv` | `LogDecisions` (false) | forwarding attempt | as S§13.3, plus: candidate count, chosen candidate's rank, ground-truth outcome, retransmission number |
| `frames.csv` | `LogFrames` (false; large, debug only) | every frame put on or taken off the channel | time (s), node, event (TX_START, RX_OK, RX_FAIL, DROP), frame type, sender, intended receiver, size (B), power level, distance (m), SNR (dB), fail reason |
| `events.csv` | `LogEvents` (true) | protocol event | time, node, event (HEAD_ELECTED, HEAD_RETIRED, HEAD_SUSPECT, HEAD_DEAD, HANDOVER_OUT/IN, VOID_NACK, NODE_DIED, CELL_CHANGED, HMIN_CHANGED), details |

**Reason codes** must be explicit, never "unknown". At least:
- **Frame receive failures:** `COLLISION`, `PER_LOSS` (SNR too low for this frame), `HALF_DUPLEX` (receiver was transmitting), `OUT_OF_RANGE`, `RECEIVER_ASLEEP`, `RECEIVER_DEAD`.
- **Packet drops:** `NO_ELIGIBLE_FORWARDER` (void), `MAX_TX_REACHED`, `HOP_BUDGET_EXHAUSTED`, `QUEUE_FULL`, `NODE_DIED_HOLDING`, `TTL_EXPIRED`.

**Consistency checks** (add as tests, and assert in debug builds at the end of each run):
- Sum over nodes of frames sent equals the summary total, and the same for every other counter.
- For each DATA attempt: successes + failures by reason = attempts.
- For each packet: delivered + dropped (with reason) + still in flight at the end = created. Report in-flight packets separately; never count them as lost.
- Retransmissions = attempts − first attempts.

Keep the logging code out of the protocol logic: protocol classes call one-line hooks (`m_metrics->OnFrameTx(...)`, `OnFrameRx(...)`, `OnDrop(node, pkt, reason)`), and `MetricsCollector` does the counting and writing. Smoke runs should enable everything except `LogFrames`; full sweeps can turn the large files off to save disk space.

---

## 3. Open questions for the supervisor (implement as Attributes with these defaults)

| # | Question | Default |
|---|---|---|
| Q1 | ERGR-EMHC cell side and G1/G2 definition (for P0) | P0: R/√3; ERGR-EMHC-g: 750 m |
| Q2 | Meaning of β = 0.9; is the 400 s election periodic? | β weights energy; periodic every 400 s |
| Q3 | 750 m with distance check, or strict 600 m? | 750 m |
| Q4 | Traffic (0.07 pkt/s/node?), bit rate, Rx/Tx power, E0, Rx charging | 0.07 pkt/s, 5 kbit/s, Tx 2 W, Rx 0.75 W, idle 8 mW, E0 300 J |
| Q5 | k = 2 fixed or configurable? | configurable, default 2 |
| Q6 | Are member sleep and header-first receive acceptable as network-layer changes? | implemented behind flags |
| Q7 | MDPI *Network* 6(2):30 (2026) virtual-cell paper | not needed for code |

---

## 4. Today's plan (in order; commit after each)

The aim of day 1 is a **running ERGR-EMHC-g + RR + CB pipeline with tests**, not results.

| Step | Work | Done when |
|---|---|---|
| **D0 Environment** (≤ 1 h) | Clone ns-3 and Aqua-Sim NG. **First read the Aqua-Sim NG README for the ns-3 version it supports**; the spec says 3.41, but use whatever version it actually builds against and record it in `CONFIRM.md`. On macOS, if the build fails for toolchain reasons after two attempts, switch to an Ubuntu 22.04 Docker container (write a `Dockerfile`). Create `contrib/cb-ergr/` with the spec's layout (S§3) plus `model/cb-ergr-params.h` and `configs/` (rule 11), `PROGRESS.md`, `doc/CONFIRM.md`, `doc/PATCHES.md`; `git init`. | An upstream Aqua-Sim NG example runs. **STOP** if no ns-3/Aqua-Sim combination builds after about 90 minutes; write what failed to `PROGRESS.md` and tell YR. |
| **D1 Learner core** (pure C++, no simulator dependency; can run in parallel with D0) | `small-matrix` (template on D), sigmoid, logistic Newton step, Laplace–Thompson sample, forgetting toward θ_cell, ACK-gate width, incremental sharing (A_own + Σ A_src), handover (planned vs unplanned), dual energy price, `ReceiptWindow`, RFC 1982 serial arithmetic. | Spec tests 1–5, 7, 8 pass, plus a new test: incremental sharing applied twice with the same source does not double count. |
| **D2 PHY/channel** | S§4.1 preamble patch; S§4.4 SNR-based link model; S§4.5 Gauss–Markov shadowing; S§4.3 explicit energy values; separate Tx/Rx/idle energy logging. | Spec test 10. A PER-vs-distance CSV and plot match the pilot within a reasonable tolerance (if the pilot is present). |
| **D3 ERGR-EMHC-g + RR** | Grid geometry (750 m), 26-neighbour candidate rule, Hmin wave from 4 surface sinks, beacons, rank-slotted election with k = 2, members → own head, void NACK, mobility (S§4.6) and localisation noise (S§4.7), metrics CSV (S§13.1 subset: PDR, delay mean/p95, energy per delivered packet split Tx/Rx/idle, control bytes per delivered by class, FND/HND, elections per hour), plus the logging of §2.6 (`nodes.csv`, `packets.csv`, `events.csv`, `frames.csv`, with reason codes and the consistency checks). Also the P0 configuration (k = 1, original rules). | The base scenario runs to completion (N = 100–250, 2,000 s smoke run). PDR falls as `CurrentSpeed` rises (spec test 16, qualitative). **Write the calibration numbers (Rx share of energy, first node death time) to `PROGRESS.md`.** |
| **D4 Feedback** | Headers (S§6) with exact `GetSerializedSize`; FeedbackManager (pending table, ACK timer → label 0, receipt bitmaps on beacons, expiry, retries with fresh hopSeq, duplicate filter); ground-truth callback (not visible to the learner). Add `links.csv`, the ACK/retransmission columns of `nodes.csv`, and `decisions.csv`. | Spec tests 9, 12, 13, plus the §2.6 consistency checks. |
| **D5 CB selector** | `SelectorCbV5` with power off, lifetime price on, sharing on, AckMode = Gated; features x1–x9 with `MarginFromRssi`; decision CSV (S§13.3). | Spec tests 2, 3, 6; smoke run of CB vs RR at `CurrentSpeed` = 1 with 3 seeds. Report the numbers without tuning anything. |
| D6 (if credits remain) | FW and QL baselines; failure detection (suspect/dead + quorum); `run_sweep.py` (YAML → parallel runs, skips existing CSVs) and `analyse.py` (mean ± 95% CI). | Each selector runs in the base scenario. |
| D7 (if credits remain) | Handover and failure injection (spec test 14); N-A/N-C flags; power patch and two-node power test (S§4.2). | Tests 11, 14. |

If you hit a design ambiguity that is not covered by §2 or the docs: choose the simplest option that keeps all methods comparable, make it an Attribute, log it in `CONFIRM.md`, and continue.

---

## 5. Things not to do
- Don't tune anything on test seeds 1–20. Training/tuning seeds are 101–110.
- Don't overhear or act on unicast frames addressed to other nodes.
- Don't count silence as success.
- Don't hide any added byte from overhead accounting (receipts, model blobs, onward-feature byte, ACKs, election and handover frames).
- Don't let engineering items (k = 2, 750 m cells, N-A, N-C, sleep) apply to CB only. They apply to every method in ERGR-EMHC-g, and P0 stays unchanged.
- Don't add deep RL, neural models, AUVs, modulation adaptation or combinatorial forwarder sets.

---

## 6. End-of-session checklist
1. All tests pass (`./test.py -s cb-ergr`), or failures are listed in `PROGRESS.md`.
1a. `doc/CHANGELOG.md` has an entry for every change made today, and `doc/CODE-GUIDE.md` matches the code (file map, "where to change X" table, how to run).
2. `git log` shows one commit per finished step.
3. `PROGRESS.md` ends with: current state, exact next step, open [CONFIRM] items, and calibration findings.
4. Push the repo (or zip it) so YR can continue in a new session.
