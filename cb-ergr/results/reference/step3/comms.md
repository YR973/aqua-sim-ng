# Step 4c: communication overhead (Step 3 runs, mean over 20 seeds and the four cells; bytes per delivered packet; airtime share of all transmitted frames)

| method | PDR | DATA bytes/pkt | beacon: fixed part | beacon: receipt bitmaps | beacon: model blob | beacon: Q value | ACK | election | handover | NACK | control total | airtime: DATA | beacon | ACK | election+handover+NACK |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| RR | 0.394 | 986 | 135 | 0 | 0 | 0 | 0 | 10 | 0 | 0 | 146 | 0.874 | 0.118 | 0.000 | 0.009 |
| RR+FB | 0.649 | 1419 | 86 | 31 | 0 | 0 | 31 | 7 | 0 | 1 | 155 | 0.896 | 0.079 | 0.020 | 0.005 |
| FW | 0.704 | 1179 | 78 | 17 | 0 | 0 | 27 | 6 | 0 | 0 | 129 | 0.896 | 0.077 | 0.022 | 0.005 |
| CB-cold | 0.679 | 1252 | 81 | 21 | 8 | 0 | 28 | 7 | 21 | 1 | 165 | 0.879 | 0.081 | 0.020 | 0.019 |
| CB-warm | 0.699 | 1213 | 79 | 19 | 6 | 0 | 28 | 6 | 20 | 1 | 159 | 0.879 | 0.080 | 0.021 | 0.019 |
| QL | 0.671 | 1253 | 83 | 21 | 0 | 7 | 28 | 7 | 0 | 0 | 145 | 0.892 | 0.083 | 0.020 | 0.005 |
| CARP-EWMA | 0.672 | 1528 | 85 | 18 | 0 | 0 | 22 | 7 | 0 | 1 | 134 | 0.917 | 0.064 | 0.013 | 0.005 |
| Oracle | 0.724 | 1150 | 76 | 18 | 0 | 0 | 28 | 6 | 0 | 0 | 128 | 0.894 | 0.079 | 0.022 | 0.006 |
