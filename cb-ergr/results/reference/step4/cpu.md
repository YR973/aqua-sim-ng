# Step 4a: CPU time in the simulator (this machine, -O3 build, double precision; mean over 4 runs at N = 300 with 10 and 30 sources)

| method | Select: mean us | p95 | max | OnSent: mean us | p95 | OnLabel: mean us | p95 | max | wall time per run (s) | peak RSS (MB) |
|---|---|---|---|---|---|---|---|---|---|---|
| RR | 1.02 | 1.61 | 80 | 0.07 | 0.10 | nan | nan | 0 | 9.1 | 36.5 |
| RR+FB | 0.95 | 1.58 | 129 | 0.07 | 0.11 | 0.09 | 0.14 | 39 | 15.0 | 39.5 |
| FW | 2.07 | 4.21 | 272 | 0.08 | 0.12 | 0.10 | 0.17 | 19 | 15.6 | 39.2 |
| QL | 1.12 | 2.07 | 203 | 0.07 | 0.11 | 0.59 | 1.02 | 69 | 15.6 | 39.4 |
| CARP-EWMA | 0.58 | 1.12 | 39 | 0.08 | 0.12 | 0.32 | 0.69 | 32 | 15.8 | 40.4 |
| CB-cold | 7.84 | 22.42 | 639 | 0.72 | 0.87 | 0.77 | 1.11 | 38 | 16.0 | 40.8 |
| CB-warm | 8.02 | 22.86 | 821 | 0.71 | 0.86 | 0.80 | 1.12 | 389 | 16.3 | 41.8 |
| CB-warm+TD | 9.06 | 23.86 | 433 | 0.70 | 0.82 | 0.69 | 1.07 | 30 | 16.2 | 42.4 |

(A call that takes 0 is not measured: a method without a learner has no OnLabel work. `max` is the largest single call and includes cold caches. wall time is for the whole 2000 s simulation of 300 nodes, all nodes, including the channel.)
