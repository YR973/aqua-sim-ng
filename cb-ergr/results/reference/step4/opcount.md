Operation counts per call (d = 9 features, K = 5 candidates, L = 1 power level, S = 1 shared sources, M = 4)

| call | MAC | add | div | sqrt | exp | pow | uniform draws | normal draws | cycles (FPU) | ms @80 MHz (FPU) | cycles (soft float) | ms @80 MHz (soft float) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CB Select (Logit/ProbStd gate) | 702 | 299 | 104 | 20 | 10 | 7 | 1 | 9 | 11073 | 0.14 | 249370 | 3.1 |
| CB Select + ThompsonDisagree (M draws) | 1122 | 299 | 180 | 20 | 30 | 27 | 1 | 45 | 32017 | 0.40 | 702010 | 8.8 |
| CB OnSent (send evidence + refactor) | 210 | 90 | 37 | 9 | 1 | 1 | 0 | 0 | 2124 | 0.03 | 49730 | 0.6 |
| CB OnLabel (one Newton step) | 99 | 0 | 19 | 0 | 1 | 0 | 0 | 0 | 772 | 0.01 | 17100 | 0.2 |
| FW Select | 285 | 210 | 40 | 10 | 5 | 0 | 0 | 0 | 3020 | 0.04 | 69450 | 0.9 |
| FW OnLabel | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0.00 | 0 | 0.0 |
| QL Select | 0 | 15 | 0 | 0 | 0 | 0 | 0 | 0 | 45 | 0.00 | 750 | 0.0 |
| QL OnLabel | 1 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 10 | 0.00 | 210 | 0.0 |
| CARP-EWMA OnLabel | 2 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 11 | 0.00 | 270 | 0.0 |
| RR Select | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 3 | 0.00 | 50 | 0.0 |

Totals for one forwarded packet with one ACK-labelled attempt (Select + OnSent + OnLabel):

| method | cycles (FPU) | ms (FPU) | cycles (soft) | ms (soft) |
|---|---|---|---|---|
| CB | 13969 | 0.17 | 316200 | 4.0 |
| CB + ThompsonDisagree | 34913 | 0.44 | 768840 | 9.6 |
| FW | 3020 | 0.04 | 69450 | 0.9 |
| QL | 55 | 0.00 | 960 | 0.0 |

Learner memory per head (float32 on the MCU, double in the simulator):

| item | floats | MCU bytes (float32) | simulator bytes (double) |
|---|---|---|---|
| theta (posterior mean) | 9 | 36 | 72 |
| theta_cell (cell prior) | 9 | 36 | 72 |
| A (own precision, symmetric half) | 45 | 180 | 360 |
| L (cached Cholesky factor, lower half) | 45 | 180 | 360 |
| shared evidence per co-head (diag d + timestamp) x S | 10 | 40 | 80 |
| clock, last discount, lambda_E, counters | 6 | 24 | 48 |
| **total model state** | 124 | **496** | 992 |
| one pending-label record (features d floats + fwd 2 B + hopSeq 2 B + packet key 4 B + send time 4 B + flags 2 B) | - | 48 | see sizeof_pending_attempt_bytes in cost.csv |
| pending records at the measured peak (80) | - | 3840 | - |

CB keeps no per-candidate state: one model per head, shared by all candidates (the candidates' features come from the neighbour table). FW keeps nothing; QL keeps one value per neighbour (4 B each); CARP-EWMA one value per neighbour (4 B each).
