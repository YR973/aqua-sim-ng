# PATCHES.md — changes to upstream Aqua-Sim NG and API differences

Rules (spec §0.4): upstream files change only for the §4 patches; each
patch is minimal and guarded by an Attribute so that the default behaviour
stays as upstream unless the patch is enabled.

Base: `rmartin5/aqua-sim-ng` master at commit `0fbc166`
("Merge pull request #37 from poseidon0z/master", NS-3.41 compatible),
built with ns-3.41.

## 1. Upstream file changes

| # | File | Change | Guard Attribute | Spec | Milestone |
|---|---|---|---|---|---|
| — | — | none yet | — | — | M0 |

Planned:

| # | File | Change | Guard Attribute | Spec |
|---|---|---|---|---|
| P1 | `model/aqua-sim-phy-cmn.cc` | `CalcTxTime`/`CalcPktSize`: `Time::FromInteger(Preamble(), Time::S)` → `Seconds(Preamble())` (no truncation) | to decide in M1: either unguarded bug-fix, or `ns3::AquaSimPhyCmn::ExactPreamble` (default false) | §4.1 |
| P2 | `model/aqua-sim-phy-cmn.{h,cc}` | read `TxPowerLevelTag` at transmit time: set Pt for the frame, charge `P_tx[level] × airtime`, stamp `SL[level]` | tag present (frames without it use the full level) | §4.2 |
| P3 | PHY / propagation | SNR-based reception decision (§4.4) — preferably as a subclass outside upstream files | `ns3::...::SnrReception` | §4.4 |

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
