# Step 4b: memory in the simulator (Step 3 runs, mean over 20 seeds)

| method | cell | neighbour rows per node: mean / p95 / max | peak pending-label records at one node | process peak RSS (MB) | wall time (s, 4 runs in parallel) |
|---|---|---|---|---|---|
| RR+FB | N100_S10 | 22.9 / 36 / 55 | 47 (max 67) | 25.2 | 2.5 |
| RR+FB | N100_S30 | 12.3 / 18 / 25 | 81 (max 107) | 28.5 | 4.3 |
| RR+FB | N300_S10 | 74.2 / 117 / 162 | 41 (max 73) | 36.8 | 11.9 |
| RR+FB | N300_S30 | 55.3 / 79 / 112 | 65 (max 88) | 41.2 | 18.3 |
| FW | N100_S10 | 23.4 / 37 / 54 | 47 (max 62) | 25.1 | 2.4 |
| FW | N100_S30 | 13.1 / 19 / 30 | 82 (max 118) | 28.2 | 4.3 |
| FW | N300_S10 | 74.6 / 118 / 157 | 46 (max 67) | 36.5 | 11.7 |
| FW | N300_S30 | 56.9 / 83 / 126 | 69 (max 102) | 40.9 | 18.5 |
| CB-cold | N100_S10 | 23.5 / 38 / 54 | 55 (max 111) | 25.4 | 2.5 |
| CB-cold | N100_S30 | 13.5 / 20 / 29 | 81 (max 106) | 28.6 | 4.5 |
| CB-cold | N300_S10 | 74.8 / 117 / 161 | 41 (max 70) | 37.9 | 12.0 |
| CB-cold | N300_S30 | 58.3 / 86 / 134 | 68 (max 94) | 42.2 | 18.7 |
| CB-warm | N100_S10 | 23.6 / 38 / 53 | 47 (max 81) | 25.6 | 2.4 |
| CB-warm | N100_S30 | 13.3 / 20 / 30 | 81 (max 97) | 28.8 | 4.5 |
| CB-warm | N300_S10 | 74.8 / 118 / 158 | 45 (max 60) | 38.7 | 11.7 |
| CB-warm | N300_S30 | 57.1 / 83 / 134 | 70 (max 102) | 43.2 | 18.5 |
| QL | N100_S10 | 23.6 / 37 / 54 | 45 (max 78) | 25.1 | 2.5 |
| QL | N100_S30 | 13.4 / 20 / 27 | 78 (max 119) | 28.2 | 4.3 |
| QL | N300_S10 | 74.7 / 117 / 156 | 48 (max 94) | 36.6 | 11.4 |
| QL | N300_S30 | 57.2 / 84 / 123 | 66 (max 99) | 41.1 | 17.7 |
| CARP-EWMA | N100_S10 | 23.0 / 36 / 50 | 57 (max 103) | 25.1 | 2.4 |
| CARP-EWMA | N100_S30 | 13.4 / 20 / 31 | 86 (max 138) | 28.3 | 4.1 |
| CARP-EWMA | N300_S10 | 73.8 / 116 / 158 | 49 (max 84) | 36.8 | 12.0 |
| CARP-EWMA | N300_S30 | 52.1 / 76 / 117 | 76 (max 138) | 42.6 | 18.3 |

Record sizes in the simulator (double precision, with padding): neighbour entry 128 B, pending attempt 224 B, held packet 96 B, CB model object 1544 B, decision 144 B.
On the wire a beacon row is the beacon header (fixed part, see `ErgrBeaconHeader::BaseBytes`); the model blob for sharing is 9 x int16 theta + 9 x uint16 diag(A) = 36 B; a planned HANDOVER carries theta and the full precision (see `ErgrHandoverHeader`).
