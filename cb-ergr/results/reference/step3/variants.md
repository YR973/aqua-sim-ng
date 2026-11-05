# Step 3, variants: FW against CB-warm (AckMode=Every), N = 300, 10 seeds 101-110

| scenario | cell | method | PDR | PDR diff to FW (95% CI) | delay mean | Tx+Rx J/pkt | first death (s) [runs] |
|---|---|---|---|---|---|---|---|
| base | N=300, 10 src | FW | 0.751 | - | 5.8 | 44.2 | 1698 [7] |
| base | N=300, 10 src | CB-warm | 0.750 | -0.000 +- 0.013 | 5.9 | 45.5 | 1764 [7] |
| base | N=300, 30 src | FW | 0.597 | - | 7.7 | 46.5 | 965 [10] |
| base | N=300, 30 src | CB-warm | 0.583 | -0.014 +- 0.015 | 8.1 | 50.0 | 967 [10] |
| a: TPre 0.1 s | N=300, 10 src | FW | 0.462 | - | 10.8 | 372.7 | 661 [10] |
| a: TPre 0.1 s | N=300, 10 src | CB-warm | 0.466 | +0.004 +- 0.044 | 11.7 | 388.1 | 673 [10] |
| a: TPre 0.1 s | N=300, 30 src | FW | 0.243 | - | 16.4 | 336.3 | 586 [10] |
| a: TPre 0.1 s | N=300, 30 src | CB-warm | 0.234 | -0.009 +- 0.016 | 17.4 | 359.6 | 589 [10] |
| b: +10 dB band (permanent) | N=300, 10 src | FW | 0.426 | - | 5.9 | 87.6 | 1659 [9] |
| b: +10 dB band (permanent) | N=300, 10 src | CB-warm | 0.422 | -0.004 +- 0.024 | 6.2 | 91.2 | 1664 [9] |
| b: +10 dB band (permanent) | N=300, 30 src | FW | 0.344 | - | 7.5 | 85.2 | 950 [10] |
| b: +10 dB band (permanent) | N=300, 30 src | CB-warm | 0.326 | -0.018 +- 0.027 | 8.0 | 92.3 | 989 [10] |
| c: +10 dB burst 800-1400 s | N=300, 10 src | FW | 0.626 | - | 5.7 | 54.4 | 1663 [7] |
| c: +10 dB burst 800-1400 s | N=300, 10 src | CB-warm | 0.638 | +0.012 +- 0.024 | 5.9 | 54.4 | 1795 [7] |
| c: +10 dB burst 800-1400 s | N=300, 30 src | FW | 0.496 | - | 7.5 | 57.2 | 969 [10] |
| c: +10 dB burst 800-1400 s | N=300, 30 src | CB-warm | 0.467 | -0.029 +- 0.016 | 8.1 | 63.3 | 962 [10] |
| d: 6 heads killed at 1000 s | N=300, 10 src | FW | 0.745 | - | 5.8 | 43.8 | 1000 [10] |
| d: 6 heads killed at 1000 s | N=300, 10 src | CB-warm | 0.756 | +0.011 +- 0.025 | 5.9 | 44.2 | 1000 [10] |
| d: 6 heads killed at 1000 s | N=300, 30 src | FW | 0.592 | - | 7.8 | 46.7 | 953 [10] |
| d: 6 heads killed at 1000 s | N=300, 30 src | CB-warm | 0.583 | -0.009 +- 0.018 | 8.0 | 49.3 | 955 [10] |

## Windowed PDR (100 s windows, pooled over seeds) and recovery times

Baseline = mean windowed PDR in 500-800 s (burst) or 600-1000 s (failures). Recovery = seconds after the reference time until two consecutive windows are within 0.02 of the baseline (n/r = not reached by 2000 s).

| scenario | cell | method | baseline PDR | PDR in event (first 100 s) | PDR in event (last 300 s) | PDR after event | recovery after event end/injection (s) |
|---|---|---|---|---|---|---|---|
| c: +10 dB burst 800-1400 s | N=300, 10 src | FW | 0.752 | 0.415 | 0.423 | 0.746 | 150 |
| c: +10 dB burst 800-1400 s | N=300, 10 src | CB-warm | 0.764 | 0.457 | 0.408 | 0.734 | 100 |
| c: +10 dB burst 800-1400 s | N=300, 30 src | FW | 0.578 | 0.310 | 0.374 | 0.667 | 0 |
| c: +10 dB burst 800-1400 s | N=300, 30 src | CB-warm | 0.550 | 0.278 | 0.366 | 0.635 | 0 |
| d: 6 heads killed at 1000 s | N=300, 10 src | FW | 0.756 | 0.697 | - | 0.742 | 300 |
| d: 6 heads killed at 1000 s | N=300, 10 src | CB-warm | 0.764 | 0.761 | - | 0.739 | 0 |
| d: 6 heads killed at 1000 s | N=300, 30 src | FW | 0.562 | 0.586 | - | 0.643 | 0 |
| d: 6 heads killed at 1000 s | N=300, 30 src | CB-warm | 0.540 | 0.565 | - | 0.650 | 0 |
