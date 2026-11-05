# TODO.md — future work (NOT implemented; recorded on 8 Nov 2026)

These are ideas the user asked to keep on file. None of them is built or planned for the current step.

## 1. FW-mismatch test
FW tuned on the BASE scenario (the weights stay fixed), then run unchanged on shifted scenarios: the noise burst, the shipping lane, the
+10 dB column, other loads (sources 10 / 30 / 45), other densities (N 100 / 150 / 300). Question: how much of CB's possible advantage over FW is
robustness to a mismatch between the tuning conditions and the deployment conditions, rather than raw PDR in the tuning scenario?
(Needs: a tuning run on base only with the same budget rule as `tune.py`, then paired runs, 20 seeds, Holm.)

## 2. Power levels (optional, supervisor)
First compare the oracle with power levels against a fixed **lowest-power-with-positive-margin** rule (a hand-made rule: for each candidate pick
the lowest level whose predicted margin is > 0, then choose among candidates by FW). Only if the oracle still wins clearly does a learner with
power control have anything to learn. (The 6 Nov / 7 Nov results: oracle+power gains +0.00..+0.04 PDR and 4-22 % energy per delivered packet;
the mechanism is the PHY's decodable/undecodable asymmetry, CHANGELOG 7 Nov 10:00.)

## 3. Possible idea, not planned: network-wide pooled CB
One shared model for the whole network instead of one per head: heads gossip increments of their sufficient statistics over the beacons
(model blob carries only the change since the last message), each head keeps a local per-head correction with shrinkage towards the pooled
model, and the discounting is shorter than today's (0.98 per 600 s) so the pooled model follows a changing channel. Motivation: a head sees only
20-34 labels per run (CHANGELOG 7 Nov 22:00), far too few to learn nine or eleven weights alone.
