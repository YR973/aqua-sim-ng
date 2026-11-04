# CHANGELOG (newest first)

One entry per change. Format fixed by the kickoff (rule 8).

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
