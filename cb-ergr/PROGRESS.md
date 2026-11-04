# PROGRESS (resume from here)

## Environment
- Workspace: `/home/user/ws/ns-3-dev` (ns-3.41, official git tag; tarball blocked). `src/aqua-sim-ng` -> this repo, `contrib/cb-ergr` -> `cb-ergr/`. `cb-ergr/scripts/setup_ns3.sh` recreates it.
- Build: `cd ns-3-dev && ./ns3 build` ; tests: `./build/utils/ns3.41-test-runner-debug --test-name=cb-ergr --verbose`
  (`./test.py -s cb-ergr` also works after a build).
- Pilot `cb_ergr_pilot.py` was **not supplied**. D2 follows spec §4.4 formulas only (see CONFIRM D1).

## Done
- D0: environment, layout, params header, docs. Upstream example + smoke example run.
- D1: learner core + 14 unit tests (all pass).

## Next
- D2 PHY/channel (preamble patch, SNR-based reception, shadowing, energy values), then D3.

## Open
- See `doc/CONFIRM.md`. Design doc v5.2 needs to be saved into `docs/`.
