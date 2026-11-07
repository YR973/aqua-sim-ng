# Summary: PDR by test × k (mean of the two cells; each cell 20 paired seeds), and the top-3 loss causes of FW

| test | k | RR | RR+FB | FW | CB-warm | Oracle | FW top-3 loss causes (share of created packets lost / failed attempts) |
|---|---|---|---|---|---|---|---|
| T1 | 1 | 0.640 | 0.822 | 0.824 | 0.826 | 0.824 | DATA–DATA collision 50%, beacon/Hello collision 20%, ACK collision 11% of failed attempts; packets: MAX_TX drops 13.4% of created |
| T1 | 2 | 0.656 | 0.845 | 0.847 | 0.846 | 0.846 | DATA–DATA collision 47%, beacon/Hello collision 24%, ACK collision 11% of failed attempts; packets: MAX_TX drops 12.7% of created |
| T2 | 1 | 0.642 | 0.797 | 0.825 | 0.816 | 0.825 | DATA–DATA collision 56%, ACK collision 14%, beacon/Hello collision 12% of failed attempts; packets: MAX_TX drops 15.0% of created |
| T2 | 2 | 0.616 | 0.800 | 0.842 | 0.828 | 0.844 | DATA–DATA collision 57%, beacon/Hello collision 15%, ACK collision 14% of failed attempts; packets: MAX_TX drops 14.9% of created |
| T3 | 1 | 0.444 | 0.642 | 0.641 | 0.648 | 0.645 | PER 41%, DATA–DATA collision 29%, beacon/Hello collision 11% of failed attempts; packets: MAX_TX drops 31.1% of created |
| T3 | 2 | 0.455 | 0.654 | 0.660 | 0.658 | 0.662 | PER 43%, DATA–DATA collision 27%, beacon/Hello collision 13% of failed attempts; packets: MAX_TX drops 31.2% of created |
| T4 | 1 | 0.415 | 0.628 | 0.663 | 0.651 | 0.675 | PER 41%, DATA–DATA collision 33%, ACK collision 7% of failed attempts; packets: MAX_TX drops 30.7% of created |
| T4 | 2 | 0.398 | 0.645 | 0.697 | 0.689 | 0.720 | PER 37%, DATA–DATA collision 36%, beacon/Hello collision 9% of failed attempts; packets: MAX_TX drops 29.1% of created |

## Same, per cell

| test | k | cell | RR | RR+FB | FW | CB-warm | Oracle |
|---|---|---|---|---|---|---|---|
| T1 | 1 | N300/10 | 0.656 ±0.024 | 0.905 ±0.013 | 0.906 ±0.013 | 0.913 ±0.016 | 0.904 ±0.013 |
| T1 | 1 | N100/30 | 0.625 ±0.016 | 0.739 ±0.024 | 0.742 ±0.022 | 0.740 ±0.021 | 0.743 ±0.025 |
| T1 | 2 | N300/10 | 0.661 ±0.023 | 0.920 ±0.014 | 0.921 ±0.013 | 0.925 ±0.015 | 0.921 ±0.011 |
| T1 | 2 | N100/30 | 0.651 ±0.020 | 0.769 ±0.025 | 0.772 ±0.025 | 0.768 ±0.020 | 0.771 ±0.024 |
| T2 | 1 | N300/10 | 0.711 ±0.016 | 0.931 ±0.012 | 0.936 ±0.010 | 0.929 ±0.010 | 0.937 ±0.012 |
| T2 | 1 | N100/30 | 0.572 ±0.011 | 0.664 ±0.019 | 0.715 ±0.018 | 0.703 ±0.019 | 0.712 ±0.012 |
| T2 | 2 | N300/10 | 0.671 ±0.015 | 0.925 ±0.008 | 0.939 ±0.010 | 0.934 ±0.009 | 0.937 ±0.010 |
| T2 | 2 | N100/30 | 0.561 ±0.011 | 0.675 ±0.017 | 0.745 ±0.018 | 0.721 ±0.018 | 0.751 ±0.016 |
| T3 | 1 | N300/10 | 0.451 ±0.033 | 0.702 ±0.029 | 0.701 ±0.029 | 0.710 ±0.030 | 0.702 ±0.032 |
| T3 | 1 | N100/30 | 0.438 ±0.015 | 0.581 ±0.018 | 0.582 ±0.021 | 0.587 ±0.020 | 0.588 ±0.023 |
| T3 | 2 | N300/10 | 0.458 ±0.031 | 0.708 ±0.029 | 0.716 ±0.027 | 0.707 ±0.032 | 0.720 ±0.026 |
| T3 | 2 | N100/30 | 0.452 ±0.019 | 0.600 ±0.022 | 0.605 ±0.021 | 0.609 ±0.021 | 0.604 ±0.022 |
| T4 | 1 | N300/10 | 0.453 ±0.031 | 0.717 ±0.028 | 0.745 ±0.025 | 0.722 ±0.025 | 0.758 ±0.020 |
| T4 | 1 | N100/30 | 0.376 ±0.014 | 0.540 ±0.015 | 0.581 ±0.019 | 0.580 ±0.018 | 0.592 ±0.017 |
| T4 | 2 | N300/10 | 0.427 ±0.028 | 0.732 ±0.019 | 0.767 ±0.018 | 0.764 ±0.017 | 0.796 ±0.015 |
| T4 | 2 | N100/30 | 0.370 ±0.012 | 0.559 ±0.014 | 0.628 ±0.019 | 0.614 ±0.020 | 0.644 ±0.018 |

## Which factor matters most? Paired differences in PDR (mean over cells; 95% CI over the 40 cell-seed pairs)

Channel = calibrated − ideal (T3−T1, T4−T2); cell size = new − old (T2−T1, T4−T3); heads = k2 − k1 (all four tests). Averaged over the other factors.

| method | channel (calibrated − ideal) | cell size (750 m − 433 m) | heads (k=2 − k=1) |
|---|---|---|---|
| RR | -0.210 ±0.006 | -0.031 ±0.009 | -0.004 ±0.005 |
| RR+FB | -0.173 ±0.008 | -0.023 ±0.009 | +0.014 ±0.004 |
| FW | -0.169 ±0.007 | +0.014 ±0.007 | +0.023 ±0.004 |
| CB-warm | -0.168 ±0.008 | +0.001 ±0.007 | +0.020 ±0.005 |
| Oracle | -0.159 ±0.007 | +0.022 ±0.008 | +0.026 ±0.004 |
