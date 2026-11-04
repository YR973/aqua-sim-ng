# PATCHES.md — changes to upstream Aqua-Sim NG and API differences

Rules (spec §0.4): upstream files change only for the §4 patches; each
patch is minimal and guarded by an Attribute so that the default behaviour
stays as upstream unless the patch is enabled.

Base: `rmartin5/aqua-sim-ng` master at commit `0fbc166`
("Merge pull request #37 from poseidon0z/master", NS-3.41 compatible),
built with ns-3.41.

## 1. Upstream file changes

All patches are guarded by a PHY Attribute that defaults to **false = upstream behaviour**, except the pure bookkeeping in P4 (no behaviour change). `CbErgrHelper::ConfigureHelper` turns P1-P3 on for CB-ERGR runs. The four files use CRLF line endings; edits preserve them.

| # | File | Change | Guard | Spec | Step |
|---|---|---|---|---|---|
| P1 | `model/aqua-sim-phy-cmn.{h,cc}` | `CalcTxTime` used `Time::FromInteger(Preamble(), Time::S)`, which truncates a fractional preamble to whole seconds. With `ExactPreamble` it uses `Seconds(Preamble())`. Also removed the `inline` keyword from the `CalcTxTime`/`CalcPktSize` declarations: they are defined in the .cc, so other libraries (our tests) could not link to them. | `ExactPreamble` (PhyCmn) | §4.1 | D2 |
| P2 | `model/aqua-sim-phy-cmn.cc` | `PrevalidateIncomingPkt` charged receive energy for `asHeader.GetTxTime()`, but the channel has overwritten that field with the **propagation delay**, so upstream charges Rx energy for the delay (about 0.3 s at 450 m) instead of the frame airtime. With `RxEnergyUsesAirtime` it charges `CalcTxTime(size)`. | `RxEnergyUsesAirtime` | §4.3, K§2.2 | D2 |
| P3 | `model/aqua-sim-phy-cmn.cc` | `StampTxInfo` overwrote the energy model's TxPower with the **acoustic** Pt (`m_pT`, 0.2818 W) on every frame, so Tx energy was Pt x airtime. With `KeepEnergyTxPower` the configured electrical TxPower (2.0 W) is kept. | `KeepEnergyTxPower` | §4.3 | D2 |
| P4 | `model/aqua-sim-energy-model.{h,cc}` | Bookkeeping only: joules drawn per state (`GetTxEnergyJ/GetRxEnergyJ/GetIdleEnergyJ`) and time the battery first reached 0 (`GetDeathTimeS`, -1 while alive). `DecrEnergy` (used by `StatusShift`) is counted as Tx. | none (no behaviour change) | §4.3, K§2.2 | D2 |

Not an upstream change: SNR-based reception is the new subclass `cbergr::CbErgrPropagation` (`model/cb-ergr-propagation.*`), selected by `AquaSimChannelHelper::SetPropagation`. It reports each frame's fate to the unchanged PHY through the received-power field (1 = decodable, 0 = below `RXThresh = 0.5`), so Aqua-Sim's own collision, half-duplex and energy logic still apply.

Planned:

| # | File | Change | Guard | Spec |
|---|---|---|---|---|
| P5 | `model/aqua-sim-phy-cmn.{h,cc}` | `TxPowerLevelTag` read at transmit time: Pt, Tx energy and source level per frame (D7, optional per K§2.3) | tag present | §4.2 |
| P6 | `model/aqua-sim-phy-cmn.cc` | Header-first receive N-A (D7, behind `HeaderFirstRx`) | `HeaderFirstRx` | D§4.2 |

## 2. API names checked against the source (spec §0.3)

Checked on 2026-10-15 against the files in this repository.

| Name in the spec | Found | Notes |
|---|---|---|
| `AquaSimPhyCmn` | yes, `model/aqua-sim-phy-cmn.h` | |
| `m_powerLevels` | yes, `std::vector<double>`, initialised to `{0.660}` | not used anywhere for transmission: `StampTxInfo` stamps `m_pT`; the `m_powerLevels[m_ptLevel]` line is commented out |
| `m_ptLevel` | yes, `int`, Attribute `PTLevel` | no effect on transmission (see above) |
| `m_pT` | yes, Attribute `PT` (default 0.2818), getter `GetPt()`, no setter | `StampTxInfo` also calls `EM()->SetTxPower(m_pT)` for non-LIBRA frames, i.e. it overwrites the energy model's TxPower with the *acoustic* Pt on every frame. P2 must take this into account. |
| `CalcTxTime` | yes, `inline Time CalcTxTime(uint32_t pktsize, std::string* modName)` | `pktsize` is in **bytes** (multiplied by 8 inside) |
| `Preamble` | yes, Attribute `Preamble` (default 0), `virtual double Preamble()` | see CONFIRM D3 |
| `AquaSimEnergyModel` | yes, `model/aqua-sim-energy-model.h`, derives from `DeviceEnergyModel` | `DecrTxEnergy(t)` charges `m_txP × t`; `SetTxPower` exists |
| `AquaSimRouting` | yes, `model/aqua-sim-routing.h`, `Object` subclass | pure virtuals: `Recv(Ptr<Packet>, const Address&, uint16_t)` and `AssignStreams(int64_t)`; helpers `SendDown(p, nextHop, delay)`, `SendUp(p)` |

Other notes:
- LIBRA MAC frames are a special case in `StampTxInfo` (they carry their own
  Tx power). CB-ERGR does not use LIBRA, so P2 will leave that branch alone.
- Upstream `test/aqua-sim-test-suite.cc` is commented out of the build
  (`# TEST_SOURCES` in `CMakeLists.txt`); not changed.

## 3. M0 acceptance (2026-10-15)

- ns-3.41 configured with `--enable-modules="aqua-sim-ng;cb-ergr;applications;point-to-point-layout"`
  (debug, examples and tests on); full build passes.
- `./ns3 run broadcastMAC_example` (upstream example) runs to completion ("fin.").
- `./ns3 run cb-ergr-smoke` runs to completion.
- `./test.py -s cb-ergr`: 1 of 1 passed.

## 4. D2 acceptance (2026-11-04)

- Spec test 10: 150 B at 5 kbit/s with TPre = 0.3 s gives 0.54 s (0.24 s with the patch off, as upstream). Passes.
- `link-per-curve` runs the real stack (broadcast MAC, SNR propagation, collisions) for 2 static nodes:
  - sigma = 0: simulated PER matches `AcousticLinkModel` (e.g. 1400 m: 0.1475 vs 0.1451; 1500 m: 0.46 vs 0.50; 400 frames per point).
  - sigma = 5 dB: simulated PER matches the shadowing-averaged analytic PER within sampling error (e.g. 1000 m: 0.168 vs 0.179; 1500 m: 0.475 vs 0.505; 600 frames per point).
  - Files: `results/reference/per_sigma0.csv`, `per_sigma5.csv`, `per_curve.png`.
- The comparison is against the analytic model, **not the pilot** (pilot not supplied; see CONFIRM D1).
