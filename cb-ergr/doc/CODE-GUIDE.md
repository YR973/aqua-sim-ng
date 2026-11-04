# CB-ERGR code guide: how to read and change the code

This guide is for YR (comfortable with Java, some C++, new to ns-3). Claude Code keeps it in sync with the code (`doc/CODE-GUIDE.md`). If a file named here doesn't exist, check `doc/CHANGELOG.md` for a rename.

> **Build status (kept up to date by Claude Code; last update: end of step D7c).**
> Working now: the whole substrate (ERGR-EMHC-g) with the `RoundRobin` selector, the SNR channel, metrics and CSVs, `cb-ergr-scenario`, and the learner maths as a pure-C++ library with tests. Hop ACKs, receipt bitmaps and retries (`FeedbackManager`) work, shown by `Selector=RoundRobin --SelectorAttributes="WithFeedback=true"` (RR+FB). `Selector=CbV5` works (`model/selector-cb-v5.{h,cc}`; functions `BuildFeatures`, `Score`, `NeedsAck`, `Select`, `OnSent`, `OnLabel`, `OnBeaconTx/Rx`; forgetting is `BeginDecision` in `cb-learner-model.h`). Baselines `FixedWeight`, `QLearning`, `CarpEwma`, `LfaQ` work (one file each, same two methods `Select` and `OnLabel`). `FittedLogistic` (A1; weights from `scripts/fit_a1.py`), `Oracle` and `GeoOracle` work, and every run reports regret against the oracle (`--ComputeRegret`). Head failure handling works: suspect tier, planned handover (`OnHandoverOut/In`), unplanned takeover (`OnRoleChange`), failure injection (`--KillHeadsAtS`). Energy savers work behind flags: `--LeanBeacons=true` (N-C) and `--HeaderFirstRx=true` (N-A). Per-frame transmit power works (`--SelectorAttributes=EnablePower=true`, with `--RegretAllPowerLevels=true` for regret). **Not written yet:** members sleeping (N-B), `run_sweep.py`/`analyse.py`.
>
> **Where things are now** (all under `contrib/cb-ergr/`):
>
> | File | What it does |
> |---|---|
> | `model/cb-ergr-params.h` | Every default value (the only place) |
> | `model/ergr-emhc-routing.{h,cc}` | Per-node routing: beacons, election, Hmin, eligible list (`GetEligible`), forwarding, member hand-off, void NACK |
> | `model/selector-cb-v5.{h,cc}` | **The method**: features, Thompson sample, score, ACK gate, learning, sharing (read this one) |
> | `model/oracle-evaluator.*`, `selector-oracle.*` | True-world hop success; the oracle selectors. **The only code allowed to read ground truth** |
> | `model/cb-features.{h,cc}` | The 9 features, shared by CB, FW, LinQ |
> | `model/selector-fixed-weight`, `selector-q-learning`, `selector-carp-ewma`, `selector-lfa-q` | The baselines (`Selector=FixedWeight|QLearning|CarpEwma|LfaQ`) |
> | `model/forwarder-selector.{h,cc}`, `selector-round-robin.{h,cc}` | The interface and the round-robin method (attributes `WithFeedback`, `AckRequest`) |
> | `model/feedback-manager.{h,cc}`, `held-packet.h` | Pending attempts, ACK/expiry timers, receipt bitmaps, retries: how a sent packet becomes a 0/1 label |
> | `model/cb-learner-model.h`, `small-matrix.h`, `receipt-window.h`, `serial-number.h`, `energy-price.h` | The learner maths: `BeginDecision` (forgetting), `SampleTheta`, `Width`, `AddSendEvidence`, `UpdateWithLabel`, sharing, handover; matrices; receipt bitmap; energy price |
> | `model/acoustic-link-model.*`, `gm-shadowing.*`, `cb-ergr-propagation.*` | The channel: link budget and PER, shadowing, the per-frame decision |
> | `model/grid-geometry.*`, `neighbour-table.*`, `ergr-headers.*`, `recent-set.h`, `localisation-noise.*`, `current-drift-mobility.*` | Grid, G1 table, packet headers, duplicate filter, noisy self-position, drifting nodes |
> | `model/metrics-collector.*` | All counters and CSV writers; protocol code only calls its `On...` hooks |
> | `model/scenario-config.*`, `helper/scenario-runner.*`, `helper/cb-ergr-helper.*` | Settings layers, the scenario builder, PHY/channel/energy set-up |
> | `examples/cb-ergr-scenario.cc` | Command-line front end (`./ns3 run "cb-ergr-scenario --N=100 --SimTimeS=2000 --OutDir=results/x"`) |
> | `examples/link-per-curve.cc`, `cb-ergr-smoke.cc` | Channel check (PER vs distance) and a link smoke test |
> | `configs/*.txt` | `base`, `p0-published`, `smoke`, `e0-10000` |
> | `test/cb-ergr-test-suite.cc` | All tests, named after the behaviour they check |
>
> Not implemented from the command list below: `--PrintAttributes`. `scripts/compare_smoke.py BINARY OUTDIR --variants RR,RR+FB,CB --rate 0.004` runs several variants on training seeds in parallel and prints a table. New: `--SelectorAttributes="Name=value;Name=value"`, `--LogDecisions=true` (decisions.csv with features, label, source, delay and the ground truth), `links.csv` (always on). `ConfigFile` and every setting listed by `--help` work.
>
> **Upstream Aqua-Sim files we patched** (all off by default; the helper turns them on): `model/aqua-sim-phy-cmn.{h,cc}`, `model/aqua-sim-energy-model.{h,cc}`. See `doc/PATCHES.md`.
>
> **Reading the first results.** With the published 300 J battery nodes die within 10-20 minutes because receiving costs ~90% of the energy; use `--ConfigFile=configs/e0-10000.txt` to compare methods. The base rate of 0.07 pkt/s saturates the channel; `configs/smoke.txt` uses 0.004 pkt/s. Details in `PROGRESS.md` and `doc/CONFIRM.md` (E13).

**The rule of thumb:** of the roughly 25 files in `contrib/cb-ergr/`, you only need **6** to understand the thesis. The rest are plumbing (headers, timers, matrix maths, the channel model) that you read only when something looks wrong.

---

## 1. Five ns-3 ideas, in Java terms

| ns-3 / C++ | Think of it as | Example |
|---|---|---|
| `Ptr<Foo>` | A Java reference (reference-counted, no `delete`) | `Ptr<ForwarderSelector> m_selector;` |
| `class Foo : public Object` + `GetTypeId()` | A Java class that registers itself, so it can be created by name | `Selector=CbV5` on the command line creates `SelectorCbV5` |
| **Attribute** (in `GetTypeId()`) | A config field with a default that can be set from the command line, with no code change | `.AddAttribute("TauW", "...", DoubleValue(0.30), ...)` |
| `Simulator::Schedule(delay, &Foo::Bar, this)` | `timer.schedule(...)` with a callback; the whole simulation is a queue of these events | ACK timers, beacon timers |
| `Header` subclass | A class with `serialize()`/`deserialize()` for a packet header | `ErgrDataHeader`, `ErgrBeaconHeader` |

`.h` files declare (like a Java interface plus fields); `.cc` files implement. Read the `.h` first.

---

## 2. Follow one packet (this is the whole protocol)

```
source node generates DATA
   │
   ▼
ErgrEmhcRouting::OnPacketToForward()        ergr-emhc-routing.cc
   │  1. build the eligible list: heads in the 26 adjacent cells,
   │     Hmin ≤ Hpkt, predicted distance ≤ R, strict progress
   │  2. if empty → void handling (NACK)
   ▼
m_selector->Select(pkt, eligible, now)      selector-*.cc   ← THE ONLY PART THAT DIFFERS BETWEEN METHODS
   │  returns: forwarder id, power level, ackRequest, features x
   ▼
send DATA (unicast) + FeedbackManager remembers the attempt
   │
   ├── ACK arrives ───────────► label 1 ─┐
   ├── ACK timer fires ───────► label 0 ─┤
   ├── beacon receipt bitmap ─► label 0/1┤   feedback-manager.cc
   └── expiry timer ──────────► label 0 ─┘
                                          ▼
                          m_selector->OnLabel(attempt, y)   ← the learner updates here
```

Every method (round robin, Q-learning, CB, oracles) plugs into the same two calls, `Select` and `OnLabel`. So **to understand a method, read its `Select` and `OnLabel` and nothing else.**

---

## 3. The six files that matter

| Read order | File | What's in it | Thesis chapter |
|---|---|---|---|
| 1 | `model/forwarder-selector.h` | The interface every method implements (about 20 lines) | Method |
| 2 | `model/selector-round-robin.cc` | The baseline: the simplest possible `Select` | Method / baseline |
| 3 | `model/selector-cb-v5.cc` | **Your contribution**: features, Thompson sample, score, ACK gate, label update, sharing, handover | Method (N1–N3) |
| 4 | `model/ergr-emhc-routing.cc` | Grid, heads, election, Hmin, eligible list, void handling, beacons | Method / substrate |
| 5 | `model/feedback-manager.cc` | How a sent packet turns into a 0/1 label | Method (N2) |
| 6 | `examples/cb-ergr-scenario.cc` | The experiment: builds nodes, mobility, sinks, installs everything, writes CSVs | Evaluation |

Inside `selector-cb-v5.cc`, these functions map one-to-one to the spec's equations:

| Function | Spec section | What it does |
|---|---|---|
| `BuildFeatures()` | §8.1 | x1–x9 from the neighbour-table row |
| `ApplyForgetting()` | §8.3 | decays A and θ toward the cell prior |
| `Select()` | §8.4–8.6 | Thompson sample → score each candidate → pick → decide the ACK request |
| `OnSent()` | §8.7 | adds send-time evidence to A |
| `OnLabel()` | §8.8 | one Newton step on θ |
| `OnBeaconTx/Rx()` | §8.10 | model sharing between the two heads |
| `OnHandoverOut/In()` | §8.10 | model handover on head change |

---

## 3b. Where the numbers live

Every parameter (cell size, range, power levels, Tx/Rx/idle power, battery, bit rate, timers, learner settings, traffic, mobility…) is defined once, in **`model/cb-ergr-params.h`**, grouped by topic and tagged with its source (`[SPEC]`, `[DESIGN]`, `[CONFIRM]`, `[TUNED]`). Values can be overridden in three layers, each beating the one before:

```
cb-ergr-params.h  (defaults; needs a rebuild)
   → configs/*.txt  (an experiment's settings; --ConfigFile=...)
      → command line  (one run; --CellSizeM=600)
```

Each run saves the final values it used in `params_used.txt` next to its results, so you can always see exactly what produced a number.

```bash
# run the base scenario but with 600 m cells, without rebuilding
./ns3 run "cb-ergr-scenario --ConfigFile=configs/base.txt --CellSizeM=600 --OutDir=results/cell600"
```

## 4. Where to change X

| I want to… | Change | Rebuild? |
|---|---|---|
| Try a different value for one run (Γ, ACK threshold, cell size, N, speed…) | Nothing: pass it on the command line (§5) | No |
| Try a set of values for an experiment | Copy `configs/base.txt`, edit it, run with `--ConfigFile=configs/yours.txt` | No |
| Change a default permanently | `model/cb-ergr-params.h`, the **only** file with parameter values; add a CHANGELOG entry | Yes |
| Add or change a learner feature | `BuildFeatures()` in `selector-cb-v5.cc`, plus `D` if the count changes, plus the beacon field in `ergr-headers.cc` if the neighbour must send something new | Yes |
| Change how candidates are chosen as eligible | `ErgrEmhcRouting::GetEligible()` (applies to **all** methods) | Yes |
| Change the score (energy weight, progress weight) | `Score()` in `selector-cb-v5.cc` | Yes |
| Change when an ACK is requested | `NeedsAck()` in `selector-cb-v5.cc` | Yes |
| Change election or head rules | `ergr-emhc-routing.cc`, election section | Yes |
| Change the channel (noise, absorption, shadowing) | `acoustic-link-model.cc`, `gm-shadowing.cc` | Yes |
| Add an output metric | `metrics-collector.cc` (count it, then add a column in `WriteSummary()`) | Yes |
| Add a new method/baseline | Copy `selector-round-robin.{h,cc}` and rename; implement `Select`/`OnLabel`; register in `wscript`/`CMakeLists.txt`; then `Selector=YourName` | Yes |

**Example: adding a selector** (the skeleton you copy):

```cpp
// selector-greedy-distance.cc: always picks the closest eligible head
Decision SelectorGreedyDistance::Select(const PacketCtx& pkt,
                                        const std::vector<Candidate>& eligible, Time now)
{
  Decision d{};
  double best = 1e18;
  for (const auto& c : eligible) {
    double dist = PredictedDistance(c.nb, now);   // helper from the base class
    if (dist < best) { best = dist; d.fwdId = c.id; }
  }
  d.powerLevel = FULL_POWER;
  d.ackRequest = false;
  return d;
}
void SelectorGreedyDistance::OnLabel(const PendingAttempt&, int, LabelSource, Time) {} // learns nothing
```

---

## 5. Running and reading results

```bash
# build only our module (fast)
./ns3 build cb-ergr

# one smoke run: CB, 100 nodes, 2000 s, current speed 1 m/s
./ns3 run "cb-ergr-scenario --Selector=CbV5 --N=100 --SimTimeS=2000 --CurrentSpeed=1 --Seed=1 --OutDir=results/smoke"

# same run with round robin, for comparison
./ns3 run "cb-ergr-scenario --Selector=RoundRobin --N=100 --SimTimeS=2000 --CurrentSpeed=1 --Seed=1 --OutDir=results/smoke"

# list every attribute you can set
./ns3 run "cb-ergr-scenario --PrintAttributes=ns3::SelectorCbV5"

# run the tests
./test.py -s cb-ergr
```

Each run writes these files to its `OutDir` (switch them on and off with the `Log...` flags):

| File | One row per | Use it to answer |
|---|---|---|
| `summary.csv` | run | How did this method do overall? (PDR, delay, energy, overhead, regret) |
| `nodes.csv` | node | Which nodes sent, received, retransmitted or dropped the most, and why? Who died first? |
| `links.csv` | sender→forwarder pair | Which links fail? Does the learner's predicted success match reality? |
| `packets.csv` | data packet | What path did a packet take, how many retransmissions, why was it lost? |
| `events.csv` | protocol event | When were heads elected, retired or declared dead? Where were voids? |
| `decisions.csv` | forwarding attempt (off by default) | Why did the learner pick this forwarder? |
| `frames.csv` | frame on the channel (off by default; large) | Low-level debugging: collisions, SNR, half-duplex losses |

Every loss has a reason code (`COLLISION`, `PER_LOSS`, `NO_ELIGIBLE_FORWARDER`, `MAX_TX_REACHED`…), so "why is PDR low?" is a `groupby`, not guesswork:

```python
import pandas as pd

run = "results/smoke/CbV5_seed1/"
packets = pd.read_csv(run + "packets.csv")
nodes   = pd.read_csv(run + "nodes.csv")

# Why were packets lost?
lost = packets[packets.delivered == 0]
print(lost.drop_reason.value_counts())

# Which nodes retransmit the most?
print(nodes.sort_values("retransmissions", ascending=False)
           [["node", "is_head_time_s", "data_attempts", "retransmissions", "hop_success_rate"]]
           .head(10))

# Compare methods across runs
import glob
summary = pd.concat(pd.read_csv(f) for f in glob.glob("results/smoke/*/summary.csv"))
print(summary[["selector", "pdr", "delay_mean_s", "energy_per_delivered_j", "ack_frames_per_delivered"]])
```

The exact column names are in the header row of each file; Claude Code keeps this table up to date.

---

## 6. Debugging: see what one node is thinking

```bash
NS_LOG="SelectorCbV5=level_debug|prefix_time|prefix_node" ./ns3 run "cb-ergr-scenario --N=30 --SimTimeS=500" 2>&1 | grep "node 7" | head -50
```

This prints each decision (candidates, pHat, width, ACK requested) for node 7. Swap the component name (`ErgrEmhcRouting`, `FeedbackManager`) to watch the other parts.

---

## 7. How to work with Claude Code on this code

- To understand something: *"Explain `Select()` in selector-cb-v5.cc line by line, and map each line to the spec section."*
- To change something: *"Change X because Y. Log it in CHANGELOG.md, update CODE-GUIDE.md if needed, run the tests."* Always give the **why**: it goes into the changelog, and you will need it in the thesis.
- To check a change: *"Show me the diff and the CHANGELOG entry."* Then run a smoke run before and after.
- Read `doc/CHANGELOG.md` (what changed and why), `doc/CONFIRM.md` (assumptions to check with the supervisor) and `PROGRESS.md` (where things stand) at the start of each session.
