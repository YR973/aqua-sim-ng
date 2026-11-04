# CB-ERGR v5: Implementation Specification (ns-3 / Aqua-Sim NG)

Author: Yousef Roudani (MSc thesis, SQU Dept. of Computer Science). Supervisor: Prof. Abderezak Touzene.
Spec version: 1.0, 15 Oct 2026.

---

## 0. Instructions for the coding agent (read first)

1. Read this whole file before writing code. Work **one milestone at a time** (§15). At the end of each milestone:
   - run its acceptance checks;
   - report the results;
   - stop for review before starting the next milestone.
2. Items marked **[CONFIRM]** are facts about the base protocol or the thesis setup that are not yet verified.
   - Do **not** guess them silently.
   - Implement each one as an ns-3 Attribute with the stated default.
   - Record each one in `CONFIRM.md` with: the item, the default used, and where it is used in the code.
3. API names for Aqua-Sim NG in this spec come from reading `rmartin5/aqua-sim-ng` master on 2026-10-14 (e.g. `AquaSimPhyCmn`, `m_powerLevels`, `m_ptLevel`, `m_pT`, `CalcTxTime`, `Preamble`, `AquaSimEnergyModel`, `AquaSimRouting`). Before using any of them:
   - open the source and confirm the name and behaviour;
   - if it differs, adapt and note it in `PATCHES.md`.
4. Upstream changes:
   - Change upstream Aqua-Sim NG files **only** for the patches in §4.
   - Keep each patch minimal and guarded by an Attribute, so that the default behaviour stays as upstream unless enabled.
   - List every upstream change in `PATCHES.md`.
5. Determinism:
   - All randomness goes through ns-3 `RandomVariableStream`s with `AssignStreams()`.
   - The same seed and run number must give identical output.
6. Language and style:
   - C++17, ns-3 coding style, `NS_LOG_COMPONENT_DEFINE` per file, configuration via `TypeId` Attributes.
   - No global mutable state.
   - Python 3 for the run scripts and analysis.
7. A reference toy model exists: `cb_ergr_pilot.py`, in the same folder as this spec. Use it for:
   - the link model (Thorp absorption, Stojanovic noise, BFSK PER, Gauss–Markov shadowing);
   - the ocean-current field.

   It is **not** a protocol reference. Where this spec and the pilot disagree, this spec wins.

---

## 1. Goal and scope

Implement the following in ns-3 with Aqua-Sim NG:

- **ERGR-EMHC**, the grid-based UWSN routing protocol of Al-Salti, Alzeidi, Day & Touzene, *Computer Networks* 162 (2019) 106869, doi:10.1016/j.comnet.2019.106869. Its forwarder-selection step must be **pluggable**.
- **CB-ERGR v5**: a forwarder-selection module for ERGR-EMHC that, per packet and per node:
  1. chooses a (forwarder, transmit-power level) pair with a discounted logistic contextual bandit using Laplace–Thompson sampling;
  2. penalises energy use with an online dual price and a residual-energy price on the forwarder;
  3. learns from delayed, censored hop feedback, using a receipt bitmap carried on beacons plus uncertainty-gated immediate ACKs;
  4. shares its model within the cell on existing beacons and hands it over on cell-head re-election.
- **Baselines and ablations** (§11), channel and mobility extensions (§4), metrics (§13), and sweep scripts (§12).

**Unchanged from ERGR-EMHC:** cells, cell-head election, records of neighbouring cell-heads (G1), Hmin maintenance, the eligibility rule `Hmin_j <= Hpkt`, and void handling.

**Out of scope:**
- deep RL or neural models;
- overhearing of other nodes' unicast data (all data is directed unicast);
- combinatorial forwarder sets;
- modulation adaptation;
- AUV mobility;
- real-sea validation.

---

## 2. Environment

- ns-3 **3.41** with Aqua-Sim NG (`https://github.com/rmartin5/aqua-sim-ng`), placed under `contrib/aqua-sim-ng` or `src/aqua-sim-ng` as its README describes.
- Build: `./ns3 configure --enable-examples --enable-tests && ./ns3 build`.
- New module: `contrib/cb-ergr/`. It depends on `aqua-sim-ng`, `mobility`, `energy`, `network` and `core`.
- No external linear-algebra library. Implement a small fixed-size dense matrix (d = 9) yourself (§8.10).

---

## 3. Module layout

```
contrib/cb-ergr/
  model/
    grid-geometry.{h,cc}            // cell indexing, neighbour cells, cell centres
    ergr-emhc-routing.{h,cc}        // AquaSimRouting subclass; ERGR-EMHC logic
    ergr-headers.{h,cc}             // DATA, ACK, BEACON, ELECTION/HANDOVER headers
    neighbour-table.{h,cc}          // G1 rows + CB extra fields
    forwarder-selector.{h,cc}       // abstract interface (§5.3)
    selector-round-robin.{h,cc}     // B1
    selector-q-learning.{h,cc}      // B2
    selector-carp-ewma.{h,cc}       // B3
    selector-lfa-q.{h,cc}           // B4
    selector-fitted-logistic.{h,cc} // A1
    selector-dlinucb.{h,cc}         // CB-lin (v4.1)
    selector-cb-v5.{h,cc}           // CB-ERGR v5 (configurable feature flags)
    selector-oracle.{h,cc}          // O, GeoOracle
    small-matrix.{h,cc}             // 9x9 dense ops, Cholesky, triangular solves
    receipt-window.{h,cc}           // per-sender highest + 32-bit mask
    feedback-manager.{h,cc}         // pending table, timers, label resolution, retries
    energy-price.{h,cc}             // dual price λ
    acoustic-link-model.{h,cc}      // Thorp, noise, SNR, BFSK PER, margin
    gm-shadowing.{h,cc}             // Gauss–Markov log-normal shadowing per link
    current-drift-mobility.{h,cc}   // current field + vertical drift (MobilityModel)
    localisation-noise.{h,cc}       // noisy self-position/velocity used in beacons
    metrics-collector.{h,cc}        // all counters + CSV writers
  helper/
    cb-ergr-helper.{h,cc}           // installs routing + selector + extensions on nodes
  examples/
    cb-ergr-scenario.cc             // the single experiment driver (CLI args = Attributes)
    two-node-power-test.cc          // §4.2 acceptance test
  test/
    cb-ergr-test-suite.cc           // unit + integration tests (§14)
  scripts/
    run_sweep.py                    // launches runs in parallel, one CSV per run
    analyse.py                      // aggregates, 95% CIs, plots
  doc/
    CONFIRM.md  PATCHES.md
```

---

## 4. Simulator patches and extensions

### 4.1 Preamble and fixed per-frame overhead (mandatory)

- Upstream `AquaSimPhyCmn` sets `m_preamble = 1.5` in the constructor, but the `Preamble` Attribute default is 0. Because Attributes are applied after construction, the effective preamble is probably 0.
- `CalcTxTime` uses `Time::FromInteger(Preamble(), Time::S)`, which **truncates** the value to whole seconds.

What to do:
- **Patch:** replace that call with `Seconds(Preamble())`.
- Set `Preamble` explicitly from the scenario (Attribute `TPre`, default **0.1 s**).
- Sweep `TPre ∈ {0, 0.1, 0.3, 0.5, 1.0}` s (§12).
- **Acceptance:** for a 150 B frame at 5 kbit/s with TPre = 0.3 s, a unit test confirms the airtime is 0.3 + 0.24 = 0.54 s.

### 4.2 Per-packet transmit-power level (mandatory for the power arm)

- Aqua-Sim NG `AquaSimPhyCmn` has `m_powerLevels`, `m_ptLevel` and `m_pT`, with a getter but no visible setter.
- **Add a `TxPowerLevelTag`** (ns-3 `Tag`) that the routing layer attaches to each DATA packet. At transmit time the PHY reads the tag and:
  - sets the transmit power for that frame from a configured list of levels;
  - charges the energy model with the matching `P_tx[level] × airtime`;
  - passes the acoustic source level `SL[level]` into reception, so that SINR/PER at the receiver depends on it.
- Frames without the tag (beacons, ACKs, control) use the **full** level.
- The header comment "MUST make sure Pt and tx_range are consistent" warns that propagation may be range-based. If reception is range-threshold only, implement §4.4 so that success depends on SNR.
- **Acceptance (`two-node-power-test.cc`):**
  - two static nodes at distances {500, 1000, 1400} m, 2000 packets per level;
  - success rate must increase monotonically with power level at 1400 m;
  - energy per transmission must match `P_tx[level] × airtime` within 1%.
  - **Stop and report if this fails.** The power arm is meaningless without it.

### 4.3 Energy model

- Set all values explicitly; do not rely on upstream defaults. Upstream has TxPower 0.660 W, RxPower 0.395 W, idle 0.008 W, while its code comments say "1.6 W / 1.2 W".
- Attributes and defaults **[CONFIRM against the group's previous ERGR-EMHC settings]**:

| Attribute | Default |
|---|---|
| `InitialEnergyJ` | 300 J |
| `TxPowerFullW` | 2.0 W |
| `TxPowerLevelScale` | {0.25, 0.5, 1.0} |
| `SourceLevelOffsetsDb` | {−6, −3, 0} |
| `RxPowerW` | 0.75 W |
| `IdlePowerW` | 0.008 W |

- A node whose energy reaches 0 is **dead**: it stops all transmission and reception, and its metrics record the time of death.

### 4.4 Link model (SNR-based success)

Implement `AcousticLinkModel` (port the pilot):

| Quantity | Formula / value |
|---|---|
| Absorption α(f) | Thorp, dB/km, at carrier f = 20 kHz (Attribute) |
| Transmission loss | TL(d) = 15·log10(d) + α·d/1000 (spreading factor k = 1.5, Attribute) |
| Noise NL | Stojanovic model (turbulence, shipping s = 0.5, wind w = 5 m/s, thermal) + 10·log10(B), with B = 5 kHz |
| SNR | SNR = SL[level] − TL(d) − NL + shadow(i, j, t) |
| Source level SL | Calibrated so that data PER = 0.5 at R = 1500 m with mean shadowing and full power (as `calibrate_sl` in the pilot) |
| Modulation | Non-coherent BFSK: Eb/N0 = SNR·B/R_bits; BER = 0.5·exp(−Eb/N0 / 2); PER = 1 − (1 − BER)^(8·L) |

- Reception succeeds with probability 1 − PER, unless there is a collision or the receiver is half-duplex busy; keep Aqua-Sim's collision handling.
- Integrate this as the reception decision in the PHY or channel. Prefer a subclass or an Attribute switch over editing upstream logic.

### 4.5 Time-correlated shadowing

`GmShadowing`: a symmetric per-link-pair Gauss–Markov process in dB.
- Parameters: σ = 5 dB (Attribute `ShadowSigmaDb`), correlation time τ = 120 s (`ShadowTauS`).
- Created lazily at first use.
- Update on access: `s ← s·ρ + σ·sqrt(1−ρ²)·N(0,1)`, with ρ = exp(−Δt/τ).
- Expose `GetShadow(i, j, t)` for the oracle.

### 4.6 Mobility

`CurrentDriftMobility`:
- horizontal current field as in the pilot `current()`;
- speed scale U (Attribute `CurrentSpeed`, swept over {0, 0.5, 1, 2} m/s);
- an Ornstein–Uhlenbeck per-node perturbation;
- vertical drift 0.1·U;
- nodes stay inside the box (reflect at the walls).
- Sinks are static at the surface.

### 4.7 Localisation noise

The position and velocity a node reports in its beacon are its true values plus Gaussian noise:
- `LocErrM` = 20 m per axis;
- `VelErrMps` = 0.1 m/s.

The node's own position used for distance prediction is the same noisy estimate. The oracle uses true positions.

---

## 5. ERGR-EMHC baseline

### 5.1 What is known (from the paper as quoted in the proposal)

- The 3D space is divided into cubic cells, and each cell has an elected **cell-head**.
- Each node, heads and non-heads alike, records the cell-heads in its neighbouring cells. This is the **G1 neighbour table**.
- Every node knows its **Hmin**, the minimum hop count to a sink. Each packet carries **Hpkt**.
- A node with a packet "looks for a forwarder from its G1 neighbor table in a round robin fashion … The selected forwarder should have Hmin less than or equal to the value Hpkt."
- Under mobility, nodes leave their cells often, so elections are more frequent.
- Voids are handled with NACKs and retransmission.

### 5.2 Unknowns to implement as Attributes and list in CONFIRM.md

| Item | Default |
|---|---|
| Cell edge length (`CellSizeM`) | R/√3 ≈ 866 m, so any two points in adjacent cells are in range — check |
| Head-election criterion | Highest residual energy, then closest to the cell centre, then lowest id |
| Election trigger and messages | Head leaves cell, head dies, or head energy < threshold |
| Hmin dissemination | Sinks broadcast Hmin = 0; each node sets Hmin = 1 + min Hmin among the G1 heads it hears; carried in beacons |
| How Hpkt is set at the source | Hpkt = Hmin(source); decremented each hop |
| Void handling / NACK procedure | If no eligible forwarder: NACK to the previous hop, which retries with Hpkt + 1 up to a limit |
| Whether G1 contains only neighbouring cell-heads | Yes |
| Whether ERGR-EMHC has periodic beacons, and their period | Yes, `BeaconPeriodS` = 50 s, with uniform random phase |
| Whether ERGR-EMHC already uses hop ACKs | No |
| Whether beacons already carry residual energy | No — CB adds 1 byte |
| MAC used in the original evaluation | Aqua-Sim NG broadcast/ALOHA MAC with MAC-level ACKs **disabled** (routing does its own feedback) |
| Scenario | 3 km × 3 km × 3 km box; R = 1500 m; data payload 150 B; per-node rate 0.07 pkt/s (pilot used 0.004); one or more surface sinks (default 4 at the quarter points); N nodes default 250 |

The **round-robin baseline must not send hop ACKs** unless ERGR-EMHC is confirmed to have them. Every CB ACK frame and every extra beacon byte must be counted as overhead (§13).

### 5.3 Forwarder-selection interface

```cpp
struct Candidate { uint32_t id; Ptr<const NeighbourEntry> nb; };
struct Decision  { uint32_t fwdId; uint8_t powerLevel; bool ackRequest;
                   std::array<double,9> x; double pMean; double width; };
class ForwarderSelector : public Object {
 public:
  virtual Decision Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now) = 0;
  virtual void OnSent(const Decision& d, Time now) {}
  virtual void OnLabel(const PendingAttempt& pa, int y, LabelSource src, Time now) {}
  virtual void OnBeaconTx(BeaconBuilder& b, Time now) {}      // may attach a model blob
  virtual void OnBeaconRx(const BeaconHeader& b, Time now) {}  // may merge a model blob
  virtual void OnKnownChange(ChangeEvent e, Time now) {}
  virtual void OnHandoverOut(HandoverBuilder& h) {}
  virtual void OnHandoverIn(const HandoverHeader& h, Time now) {}
  virtual bool UsesFeedback() const { return true; }           // RR returns false
};
```

`ErgrEmhcRouting` owns:
- the G1 table;
- `FeedbackManager` (pending table, timers, receipts, retries);
- the selector (`Ptr<ForwarderSelector>`, chosen by the Attribute `Selector`).

Routing logic must not branch on the selector type.

---

## 6. Packet formats (all serialised with ns-3 `Header`)

### 6.1 DATA routing header

| Field | Type | Notes |
|---|---|---|
| senderId | uint16 | this hop's sender |
| forwarderId | uint16 | intended receiver (unicast) |
| sourceId | uint16 | E2E key part 1 |
| sourceSeq | uint16 | E2E key part 2 |
| hpkt | uint8 | ERGR hop budget |
| hopSeq | uint16 | per (sender, forwarder) attempt number, random start, serial arithmetic (RFC 1982) |
| flags | uint8 | bit0 AR (ACK request), bit1 priority, bits2–3 power level |
| genTime | uint32 (ms) | for E2E delay |

### 6.2 ACK header (sent only when AR = 1)

ackerId uint16, senderId uint16, hopSeq uint16.

### 6.3 BEACON header (broadcast every `BeaconPeriodS`, random initial phase)

Base fields:

| Field | Size | Notes |
|---|---|---|
| nodeId | uint16 | |
| cellId | uint16 | |
| isHead | uint8 | |
| hmin | uint8 | |
| pos (x, y, z) | 3 × int16, 1 m resolution | noisy (§4.7) |
| vel (vx, vy, vz) | 3 × int16, mm/s | noisy (§4.7) |
| battery | uint8 | 0–255 = 0–100 % |
| queueLen | uint8 | |
| nReceipts | uint8 | |

Then `nReceipts` × ReceiptEntry {senderId uint16, highest uint16, mask uint32}. Include an entry for every upstream sender heard within the last `ReceiptKeepS` = 3 × BeaconPeriodS.

Then an optional ModelBlob {flag uint8; theta int16[9] with scale `ThetaScale` = 1/1024; diagA uint16[9] with scale `DiagScale` = 1/16, saturating}.

### 6.4 ELECTION / HANDOVER

Use ERGR-EMHC's election messages as they are. When the outgoing head sends its last message, append HandoverBlob {theta int16[9]; upper-triangular A, 45 × uint16 at `DiagScale`, saturating; off-diagonal entries as int16 at the same scale}.

**[CONFIRM]** which ERGR message carries it. Default: a dedicated HANDOVER unicast to the newly elected head, counted as control overhead.

### 6.5 Size accounting

Every header implements `GetSerializedSize()` exactly. `MetricsCollector` counts bytes per class: data payload, data header, ACK, beacon base, receipt entries, model blob, handover.

---

## 7. CB-ERGR v5 per-node state

```cpp
// in SelectorCbV5
static constexpr int D = 9;
Vec9   thetaHat;            // init = thetaCell (zeros if none)
Mat9   A;                   // init = lambda0 * I
Vec9   thetaCell;           // cell prior
Time   tLastDiscount;
double logdetAtLastSync;
double lambdaE;             // energy dual price, init LambdaInit
double energyAtLastTick;
std::map<uint32_t, PeerBlob> recentBlobs;   // same-cell peers' blobs, with receive time

// in FeedbackManager
std::map<HopKey, PendingAttempt> pending;   // HopKey = (fwdId, hopSeq)
std::map<uint32_t, uint16_t>     nextSeq;   // per forwarder, random initial value
std::map<E2EKey, Ptr<Packet>>    buffer;    // copies for retransmission
std::map<uint32_t, ReceiptWindow> receipts; // per upstream sender (receiver side)
LruSet<E2EKey>                   recentlySeen; // duplicate filter, capacity 1024, TTL 600 s

struct PendingAttempt { Vec9 x; uint32_t fwdId; uint8_t power; uint16_t hopSeq;
                        E2EKey pkt; Time tSend; bool ar; uint8_t attempt;
                        EventId ackTimer, expiryTimer; };
```

### 7.1 NeighbourEntry (G1 row)

Fields: id, cellId, isHead, hmin, pos, vel, beaconTime, firstHeard, battery, queueLen.

The row is removed when `now − beaconTime > NeighbourTimeoutS` (= 3 × BeaconPeriodS). Removing a row raises the `NodeLost` known-change event.

### 7.2 ReceiptWindow update (receiver side)

```
on receive hopSeq s from sender u:
  if no window: highest=s, mask=1
  elif serialGreater(s, highest): k = s - highest (mod 2^16)
       mask = (k >= 32) ? 1 : ((mask << k) | 1); highest = s
  else: k = highest - s; if k < 32: mask |= (1 << k)
```

---

## 8. Algorithms (exact)

### 8.1 Features x(j, p) ∈ [0, 1]^9

All features are computed from the G1 row and the node's own noisy position.

```
posNow   = pos_j + vel_j * (now - beaconTime_j)
dHat     = max(1, |posNow - myPos|)
x1 = clip(dHat / R)                                    // distance
x2 = (hmin_j < hpkt) ? 1 : 0                           // hop progress
x3 = clip(((myDepth - depth_j)/R + 1)/2)               // depth advantage
x4 = battery_j                                         // residual energy fraction
x5 = clip((now - firstHeard_j) / EntryAgeScaleS)       // EntryAgeScaleS = 1000 s
x6 = clip(queueLen_j / QMax)                           // QMax = 10
x7 = clip((closingSpeed/ VMax + 1)/2)                  // closingSpeed = -d|r|/dt from relative velocity; VMax = 2 m/s
x8 = clip((margin(j,p) + 10) / 30)                     // SNR margin, §8.2
x9 = 1                                                 // intercept
```

`clip` means clip to [0, 1]. Only x8 depends on p.

### 8.2 Predicted SNR margin

```
margin(j,p) = SL[p] - TL(dHat) - NL - SnrReqDb
```

Uses the §4.4 formulas **without shadowing** (the node can't know it). `SnrReqDb` is the SNR at which data PER = 0.1 for the configured packet length; compute it once at start-up.

Optional variant (Attribute `MarginFromRssi`, default false): estimate TL from the measured RSSI of j's last beacon, assuming link symmetry.

### 8.3 Forgetting (lazy; applied before any use or update of A or θ̂)

```
w = Gamma ^ ((now - tLastDiscount) / T0)     // Gamma = 0.98, T0 = 60 s
A        = w*A + (1-w)*Lambda0*I             // Lambda0 = 1
thetaHat = w*thetaHat + (1-w)*thetaCell
tLastDiscount = now
```

### 8.4 Thompson sample (once per decision)

```
L = cholesky(A)                    // A = L L^T
z ~ N(0, I_9)
thetaTilde = thetaHat + V * backSolve(L^T, z)   // V = ExplorationScale = 1.0
```

### 8.5 Score

```
pHat   = sigmoid(thetaTilde · x)
Etx(p) = TxPowerFullW * TxPowerLevelScale[p] * airtime(dataBytes)
phi(e) = (1/max(e, 0.01))^Kappa - 1                  // Kappa = 1
score  = pHat * (1 - KappaP + KappaP * x2) - lambdaE * Etx(p) - Mu * phi(battery_j)
// KappaP = 0.5, Mu = 0.1
```

Pick the argmax over every eligible j and every p in the enabled levels. Break ties by lower Etx, then lower id.

### 8.6 ACK request

```
width = norm(forwardSolve(L, x))            // = sqrt(x^T A^{-1} x)
AR = (attempt > 1) || (width > TauW) || (priority) || (U(0,1) < QAudit)
// TauW = 0.30, QAudit = 0.10, priority = (hpkt > myHmin) || pkt.priorityFlag
```

### 8.7 Send-time evidence

```
pBar = sigmoid(thetaHat · x)        // mean, not sample
A += pBar*(1-pBar) * x x^T
```

### 8.8 Label update (one Newton step), called by FeedbackManager::Resolve

```
applyForgetting(now)
resid = y - sigmoid(thetaHat · x)
thetaHat += solve(A, resid * x)      // via Cholesky of current A
```

The first label for a HopKey wins. Later labels for the same key are ignored and counted as `duplicateLabels`.

### 8.9 Energy dual price (timer every `PriceTickS` = 600 s)

```
spendRate = (energyAtLastTick - energyNow) / PriceTickS
rho       = InitialEnergyJ / TargetLifetimeS            // TargetLifetimeS = 1e5 s [CONFIRM per scenario]
lambdaE   = max(0, lambdaE + Eta * (spendRate - rho))   // Eta = 20, LambdaInit = 0.25
energyAtLastTick = energyNow
```

### 8.10 Sharing

**At own beacon:**
```
logdet = 2 * sum(log(L_ii))   // L from cholesky(A) after applyForgetting
if (logdet - logdetAtLastSync > TauSync) { attach blob(thetaHat, diag(A)); logdetAtLastSync = logdet; }
// TauSync = 0.7
```

**On a beacon with a blob from a same-cell node k:** store it in `recentBlobs[k]`. Drop entries older than `BlobKeepS` = 10 × BeaconPeriodS. Then recompute:
```
accept_k  = (trace(A_self) < MatureTrace) || cos(thetaHat, theta_k) > CosGate
            // MatureTrace = 50, CosGate = 0.5
thetaCell = sum_k accept_k * s_k * theta_k / sum_k accept_k * s_k,   s_k = sum(diagA_k)
            // unchanged if no accepted k
```

`thetaCell` acts only through forgetting (§8.3), and through initialisation when a node boots or joins a cell with no data of its own (thetaHat = thetaCell).

**Handover:**
- The outgoing head writes its blob (θ̂, full A).
- The incoming head sets `thetaHat = θ_old` and `A = BetaHandover * A_old` (BetaHandover = 0.7).

### 8.11 Known-change inflation

`OnKnownChange(e)` with e ∈ {HeadChangedInNeighbourCell, NodeLost, OwnCellChanged}:
```
A = BetaInflate * A + (1 - BetaInflate) * Lambda0 * I     // BetaInflate = 0.7
```

### 8.12 Small linear algebra (small-matrix.h)

Implement for 9×9 symmetric matrices:
- `cholesky` (if a pivot is ≤ 0, add jitter 1e-9·I and retry; after 3 failures reset A = Lambda0·I and log a warning);
- `forwardSolve`, `backSolve`, `solveSPD`;
- `addOuter(A, x, c)`, `logdetFromChol`, `trace`.

Unit-test each one against hand-computed values.

### 8.13 sigmoid

Clamp the argument to [−30, 30].

---

## 9. Events, timers and the feedback state machine

### 9.1 `OnPacketToForward(pkt, attempt)`

1. `eligible = { j ∈ G1 : j.isHead && hmin_j <= pkt.hpkt && alive }`.
   - If empty, call ERGR void handling (§5.2). No learning happens.
2. If `selector->UsesFeedback()` is false (RR):
   - send without AR and with no pending entry;
   - ERGR's own reliability applies.
3. Otherwise: `d = selector->Select(...)`.
4. `hopSeq = ++nextSeq[d.fwdId]`. Send DATA with AR = d.ackRequest and `TxPowerLevelTag = d.powerLevel`.
5. `selector->OnSent(d)` (send-time evidence).
6. Create the `PendingAttempt` and `buffer[pkt.e2e] = copy`.
7. If AR, schedule `ackTimer` at `ackTimeout(d)`:
   ```
   ackTimeout = airtime(DATA) + 2*dHat/1500 + airtime(ACK) + TProcS + TGuardS
   // TProcS = 0.05, TGuardS = 0.2
   ```
8. Schedule `expiryTimer` at `MExpiryS` = 3 × BeaconPeriodS.

### 9.2 `OnDataReceived` (I am forwarderId)

1. `receipts[sender].mark(hopSeq)`.
2. If AR, send an ACK (unicast, full power).
3. If `recentlySeen.insert(e2e)` succeeds:
   - if I am a sink, deliver and record metrics;
   - otherwise call `OnPacketToForward(pkt with hpkt−1 and new hop fields, 1)`.
4. Otherwise it is a duplicate: count it, do not forward.

### 9.3 `OnAckReceived`

`Resolve((ackerId, hopSeq), 1, LabelSource::Ack)`.

### 9.4 `OnAckTimeout(key)`

If still pending, call `RetryOrDrop(pa)`. **The label stays pending.**

### 9.5 `OnBeaconReceived(b)`

1. Update G1 (raise known-change events on a head change or a new node).
2. For each receipt entry whose senderId is me, for each pending attempt to b.nodeId:
   - `k = serialDiff(highest, hopSeq)`;
   - if 0 ≤ k < 32, read the bit;
   - bit 1: `Resolve(key, 1, Bitmap)`;
   - bit 0: `Resolve(key, 0, Bitmap)`, then `RetryOrDrop(pa)` if the packet is still buffered.
3. `selector->OnBeaconRx(b)`.

### 9.6 `OnExpiry(key)`

If still pending:
- `Resolve(key, 0, Expiry)`;
- if `!pa.ar`, call `RetryOrDrop(pa)`.

### 9.7 `Resolve(key, y, src)`

1. Look up the attempt; if it is not pending, ignore it.
2. Remove it and cancel its timers.
3. `selector->OnLabel(pa, y, src)`.
4. If y == 1, `buffer.erase(pa.pkt)`.
5. Log a decision row (§13.3).

### 9.8 `RetryOrDrop(pa)`

- If the packet is not in the buffer, return (another attempt already succeeded).
- If `pa.attempt < MaxTx` (= 3), call `OnPacketToForward(pkt, attempt+1)`. This is a fresh decision with a fresh hopSeq.
- Otherwise, remove it from the buffer and hand it to ERGR drop/NACK handling.

### 9.9 Beacon timer

Build the base fields and the receipt entries, call `selector->OnBeaconTx(builder)`, then broadcast.

### 9.10 Price timer

§8.9.

### 9.11 Election

- When this node loses headship: `selector->OnHandoverOut` → send the HANDOVER.
- On receiving a HANDOVER: `OnHandoverIn`.

### 9.12 Design assumptions (document in the thesis)

- Each transmission attempt has its own hopSeq.
- No reordering on a single sender→forwarder link, so a 0 bit below `highest` is an explicit loss.
- First label wins.
- Retries always request an ACK.

---

## 10. Attributes and defaults (all exposed on the command line)

| Group | Attribute | Default |
|---|---|---|
| PHY | `TPre` | 0.1 s |
| PHY | `BitRate` | 5000 bit/s |
| PHY | `CarrierKHz` | 20 |
| PHY | `BandwidthHz` | 5000 |
| PHY | `RangeM` | 1500 |
| Energy | see §4.3 | |
| Channel | `ShadowSigmaDb` | 5 |
| Channel | `ShadowTauS` | 120 |
| Channel | `SpreadingK` | 1.5 |
| Mobility | `CurrentSpeed` | 1.0 m/s |
| Mobility | `LocErrM` | 20 |
| Mobility | `VelErrMps` | 0.1 |
| ERGR | see §5.2 | |
| Beacon | `BeaconPeriodS` | 50 |
| Learner | `Gamma` | 0.98 |
| Learner | `T0` | 60 s |
| Learner | `Lambda0` | 1 |
| Learner | `ExplorationScale` | 1.0 |
| Score | `KappaP` | 0.5 |
| Score | `Mu` | 0.1 |
| Score | `Kappa` | 1 |
| Price | `LambdaInit` | 0.25 |
| Price | `Eta` | 20 |
| Price | `PriceTickS` | 600 |
| Price | `TargetLifetimeS` | 1e5 |
| ACK | `TauW` | 0.30 |
| ACK | `QAudit` | 0.10 |
| ACK | `MaxTx` | 3 |
| ACK | `TProcS` | 0.05 |
| ACK | `TGuardS` | 0.2 |
| ACK | `MExpiryS` | 150 |
| Sharing | `TauSync` | 0.7 |
| Sharing | `CosGate` | 0.5 |
| Sharing | `MatureTrace` | 50 |
| Sharing | `BetaHandover` | 0.7 |
| Sharing | `BetaInflate` | 0.7 |
| Feature flags (`SelectorCbV5`) | `EnablePower` | true |
| | `PowerLevels` | 3 |
| | `EnableLifetime` | true |
| | `EnableSharing` | true |
| | `AckMode` | Gated (Every \| Gated \| BitmapOnly) |
| | `Link` | Logistic (Logistic \| Linear) |
| Run | `Selector` | CbV5 |
| Run | `SimTimeS` | 20000 |
| Run | `Seed` | 1 |
| Run | `Run` | 1 |
| Run | `OutDir` | results/ |

- With `EnablePower=false`, use only the full power level.
- With `EnableLifetime=false`, set lambdaE = Mu = 0.
- `AckMode=Every` means AR = 1 always. `BitmapOnly` means AR = 0 except on retries.

---

## 11. Baselines and ablations (each one is a `Selector` value)

| ID | Selector | Definition |
|---|---|---|
| B1 | `RoundRobin` | ERGR-EMHC as published: cycle through eligible G1 heads, full power, no CB feedback. |
| B2 | `QLearning` | Tabular Q per neighbour id. **[CONFIRM]** against the group's internal QL-ERGR-EMHC proposal. Default: QELAR-style. Q(j) ← Q(j) + αq·(r + γq·V_j − Q(j)); V_j = max Q advertised by j in its beacon (+2 B); ε-greedy ε = 0.1; αq = 0.3; γq = 0.8; r = hop success − cost 0.1; init Q = 1 + Hmin-based heuristic. Uses the same feedback channel as CB (AckMode = Every). |
| B3 | `CarpEwma` | Per-neighbour EWMA of hop success (β = 0.2) × progress weight; residual energy breaks ties; full power. |
| B4 | `LfaQ` | Linear Q(x) = wᵀx on the same 9 features (power fixed at full); target r + γq·V̂_j, where V̂_j is advertised in the beacon; semi-gradient TD(0), step 0.05; ε-greedy 0.1. Tests the value of bootstrapping. |
| A1 | `FittedLogistic` | Weights fitted **offline** (scripts/fit_a1.py, L2 logistic regression) on (x, y) decision logs from RR runs on **training seeds only**, then fixed; argmax of σ(wᵀx)·progressWeight, with energy terms as in CbV5 when power is enabled. |
| CB-lin | `DLinUcb` | v4.1 as in the proposal. V, Ṽ (9×9), b; θ = V⁻¹b; j* = argmax θᵀx + β·sqrt(xᵀV⁻¹ṼV⁻¹x), β = 0.4; time-based forgetting w = γ^(Δt/T0): V ← wV + (1−w)λI, Ṽ ← w²Ṽ + (1−w²)λI, b ← wb; at send, add xxᵀ to V and Ṽ; on ACK, b += r·x with r = s·(1−κ+κ·x2), κ = 0.5; timeout gives r = 0; round robin for the first 20 outcomes; full power; AckMode = Every. |
| P-ladder | `CbV5` + flags | (i) Link=Logistic, no power, no lifetime, no sharing, AckMode=Every; (ii) + EnablePower; (iii) + EnableLifetime; (iv) + EnableSharing; (v) + AckMode=Gated. Also AckMode=BitmapOnly, and Gamma = 1 (no forgetting). |
| O | `Oracle` | Uses true positions, true shadowing and the §4.4 model. Picks argmax over (j, p) of the true expected success × progress weight − the same energy terms. Used for regret. |
| GO | `GeoOracle` | Like O, but uses the mean (shadow-free) success at the true distance. |

All learners use the same eligibility rule, the same feedback manager and the same overhead accounting.

---

## 12. Scenarios and sweeps

- **Base scenario** (defaults §5.2/§10): 3 × 3 × 3 km, N = 250, 4 surface sinks, R = 1500 m, 150 B, 0.07 pkt/s per node **[CONFIRM]**, SimTimeS = 20000 s, warm-up 2000 s excluded from metrics.
- **Seeds:**
  - test seeds 1–20 (≥ 20 per point, 95% CI by t-distribution);
  - training seeds 101–110, used only for A1 fitting and any tuning;
  - give every learning method the same tuning budget.
- **Sweeps** (one factor at a time around the base):

| Factor | Values |
|---|---|
| `CurrentSpeed` | {0, 0.5, 1, 2} |
| `ShadowSigmaDb` | {0, 3, 5, 8} |
| `TPre` | {0, 0.1, 0.3, 0.5, 1.0} |
| `BeaconPeriodS` | {25, 50, 100} |
| `Gamma` | {0.95, 0.98, 0.995, 1.0} |
| `QAudit` | {0, 0.05, 0.1, 0.2} |
| `TauW` | {0.2, 0.3, 0.5} |
| N | {150, 250, 400} |

- **Events scenario:** forced head kill at t = 8000 s in 5 random cells, to measure recovery time.
- `run_sweep.py` takes a YAML list of (selector, flags, sweep), runs in parallel, skips existing outputs, and writes one summary CSV per run.

---

## 13. Metrics and output

### 13.1 Per-run summary CSV (one row)

- scenario params, selector, flags, seed;
- **PDR**;
- mean and p95 **E2E delay**;
- **data tx per delivered** packet;
- **ACK frames per delivered** packet;
- **control bytes per delivered** packet (beacon base, receipts, blobs, handover, ACK — each also separately);
- **energy per delivered** packet (J), total and split by tx/rx/idle;
- **FND** (first node dead) and **HND** (half nodes dead) times;
- **Jain index** of residual energy at the end;
- **elections per hour**; mean **head tenure**;
- **mean regret per decision** against O (O's expected utility − chosen expected utility, both computed with the true model);
- **label stats:** fraction resolved by Ack / Bitmap / Expiry; mean and p95 label delay; **false-negative rate** (label 0 but ground truth says received); duplicate labels;
- **AR rate** (fraction of attempts with AR = 1);
- **recovery time** (events scenario), defined as: for nodes forwarding into an affected cell, the time from the kill until their 300 s moving-window hop success rate returns within 5 percentage points of its mean over the 1000 s before the kill;
- per-node compute cost (mean µs per Select, measured with a steady clock and reported separately; not used for logic) and memory (bytes of selector state).

### 13.2 Ground truth

The simulator knows whether each attempt was actually received by the forwarder. Record this in the pending attempt via a callback from the receiver, **without** feeding it to the learner.

### 13.3 Optional decisions CSV (`--LogDecisions=true`)

One row per attempt:

```
t, node, fwd, power, attempt, AR, x1..x9, pBar, width, score, lambdaE, label, labelSource, labelDelay, truth
```

### 13.4 analyse.py

Aggregates over seeds (mean ± 95% CI) and produces:
- PDR / energy / delay against each sweep factor;
- ACK frames per delivered against Gamma and QAudit;
- regret over time;
- recovery-time bars;
- the ablation table (rows = §11 ladder).

---

## 14. Tests (ns-3 TestSuite `cb-ergr`)

**Unit tests:**
1. small-matrix: Cholesky, solves and logdet on known SPD matrices; jitter fallback.
2. sigmoid clamp; logistic Newton step decreases the loss on a toy 2-feature dataset (100 samples, converges to within 0.05 of the sklearn solution).
3. Thompson sample: over 10,000 draws the empirical covariance ≈ A⁻¹ (relative error < 5%).
4. ReceiptWindow: in-order, gaps, wrap-around at 65535 → 0, jumps ≥ 32.
5. Serial-number comparison (RFC 1982) edge cases.
6. Feature builder: hand-computed x for a fixed G1 row.
7. Forgetting: with no data, A → Lambda0·I and θ̂ → θ_cell as Δt → ∞.
8. Energy price moves in the right direction for over- and under-spend, and never goes below 0.
9. Header serialise/deserialise round trips; `GetSerializedSize` matches the bytes written.
10. Airtime with the TPre patch (§4.1).

**Integration tests:**
11. Two-node power test (§4.2).
12. Three-node line (S → F → sink) with a forced 30% loss: every attempt receives exactly one label; the bitmap produces explicit 0s; retries get new hopSeqs; no duplicate is forwarded downstream.
13. Lost-ACK case: drop the ACK deterministically. The data attempt must later be labelled 1 from the bitmap, and the retry must be deduplicated at F.
14. Handover: kill a head; the new head's θ̂ equals the old head's within quantisation error.
15. Determinism: two runs with the same seed give byte-identical summary CSVs.
16. B1 regression: with `Selector=RoundRobin`, behaviour matches ERGR-EMHC's published trends qualitatively (PDR falls as speed rises). **[CONFIRM]** with the user's reference numbers if available.

---

## 15. Milestones (stop and report after each)

| # | Deliverable | Acceptance |
|---|---|---|
| M0 | Environment builds; aqua-sim-ng examples run; CONFIRM.md and PATCHES.md started | `./ns3 run` of an upstream example succeeds |
| M1 | §4.1 preamble patch, §4.4 link model, §4.5 shadowing, §4.3 energy settings | Tests 1, 10; link PER curve plotted against the pilot's |
| M2 | §4.2 per-packet power | Test 11 passes (**gate**: stop if it fails) |
| M3 | Grid geometry, ERGR-EMHC routing with RoundRobin, beacons, elections, Hmin, void handling; §4.6–4.7 mobility and localisation noise | Test 16; base scenario runs to completion; metrics CSV written |
| M4 | FeedbackManager, headers, receipts, ACKs, retries, ground truth; DLinUcb (CB-lin) | Tests 4, 5, 9, 12, 13 |
| M5 | SelectorCbV5: logistic, Thompson, features, scoring (no power, no lifetime, no sharing) | Tests 2, 3, 6, 7; beats RR on PDR in base scenario at speed 1 m/s, or report if not |
| M6 | + power, + lifetime price | Test 8; ablation rows (ii), (iii) |
| M7 | + sharing, handover, known-change inflation | Test 14; events scenario recovery metric |
| M8 | AckMode Gated / BitmapOnly | AR-rate and ACK-frames metrics; ladder row (v) |
| M9 | Baselines B2, B3, B4, A1 (fit script), O, GO | All selectors run in the base scenario |
| M10 | run_sweep.py, analyse.py, full sweeps | Ablation table + sweep plots generated from CSVs |

If time is short, stop after M8. Each milestone already gives usable results.

---

## 16. Things not to do

- Do not let the learner read ground truth, true positions or true shadowing (only O/GO may).
- Do not overhear or act on unicast frames addressed to other nodes.
- Do not count silence as success anywhere.
- Do not hide any added byte from overhead accounting.
- Do not tune on test seeds.
- Do not add dependencies beyond ns-3 and Aqua-Sim NG for the C++ code (Python analysis may use numpy, pandas, matplotlib, scikit-learn).
