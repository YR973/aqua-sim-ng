# PROGRESS (resume from here)

## Environment
- Workspace: `/home/user/ws/ns-3-dev` (ns-3.41, official git tag; tarball blocked). `src/aqua-sim-ng` -> this repo, `contrib/cb-ergr` -> `cb-ergr/`. `cb-ergr/scripts/setup_ns3.sh` recreates it.
- Build: `cd ns-3-dev && ./ns3 build` ; tests: `./build/utils/ns3.41-test-runner-debug --test-name=cb-ergr --verbose`
  (`./test.py -s cb-ergr` also works after a build).
- Pilot `cb_ergr_pilot.py` was **not supplied**. D2 follows spec §4.4 formulas only (see CONFIRM D1).

## Done
- D0: environment, layout, params header, docs. Upstream example + smoke example run.
- D1: learner core + 14 unit tests (all pass).
- D2: PHY/channel. Patches P1-P4, `CbErgrPropagation` (SNR/PER per frame), shadowing, link model, helper, `link-per-curve` example. 17 test cases pass. Simulated PER matches the analytic curve (sigma 0 and 5 dB); see `results/reference/`. Pilot comparison still pending.

## Next
- D3 ERGR-EMHC-g + RoundRobin (grid, beacons, election k=2, Hmin wave, void NACK, mobility, localisation noise, metrics + logging, P0 config). This is the biggest step; split into D3a (geometry, nodes, mobility, beacons, Hmin, RR forwarding, packets/summary metrics) and D3b (election, void NACK, events/nodes/frames logs, calibration numbers).

## Open
- See `doc/CONFIRM.md`. Design doc v5.2 needs to be saved into `docs/`.
