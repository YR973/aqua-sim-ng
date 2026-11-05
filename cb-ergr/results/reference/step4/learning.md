# Step 4d: CB learning behaviour (N = 300, 10 seeds 101-110, Csma, calibrated channel)

## Labels, ACK requests, PDR

| N | src | method | PDR | labels per head | censored share of attempts | false-negative rate of the 0 labels | ACK-request rate | ACK frames/pkt | decisions with >= 2 viable arms | gap of the two best arms (mean) |
|---|---|---|---|---|---|---|---|---|---|---|
| 300 | 10 | FW | 0.751 | 23 | 0.255 | 0.002 | 1.00 | 4.00 | 0.52 | 0.052 |
| 300 | 10 | CB-cold | 0.725 | 20 | 0.293 | 0.001 | 1.00 | 4.07 | 0.55 | 0.051 |
| 300 | 10 | CB-warm | 0.750 | 21 | 0.262 | 0.002 | 1.00 | 4.06 | 0.52 | 0.051 |
| 300 | 10 | CB-warm+Logit | 0.752 | 21 | 0.262 | 0.003 | 1.00 | 4.06 | 0.53 | 0.052 |
| 300 | 10 | CB-warm+ProbStd | 0.758 | 21 | 0.323 | 0.011 | 0.85 | 3.81 | 0.51 | 0.057 |
| 300 | 10 | CB-warm+TD | 0.770 | 21 | 0.354 | 0.016 | 0.72 | 3.77 | 0.51 | 0.056 |
| 300 | 30 | FW | 0.597 | 34 | 0.384 | 0.002 | 1.00 | 4.52 | 0.54 | 0.063 |
| 300 | 30 | CB-cold | 0.564 | 32 | 0.413 | 0.002 | 1.00 | 4.73 | 0.55 | 0.055 |
| 300 | 30 | CB-warm | 0.583 | 33 | 0.399 | 0.002 | 1.00 | 4.77 | 0.54 | 0.056 |
| 300 | 30 | CB-warm+Logit | 0.581 | 32 | 0.401 | 0.003 | 1.00 | 4.80 | 0.54 | 0.056 |
| 300 | 30 | CB-warm+ProbStd | 0.560 | 32 | 0.428 | 0.005 | 0.90 | 4.65 | 0.52 | 0.057 |
| 300 | 30 | CB-warm+TD | 0.543 | 32 | 0.445 | 0.013 | 0.76 | 4.58 | 0.55 | 0.057 |

## Regret over time, N = 300, 10 sources (mean per decision of the 10 seeds; regret in utility units = success x progress)

| window start (s) | FW vs oracle | CB-cold vs oracle | CB-warm vs oracle | CB-cold vs FW | CB-warm vs FW | mean posterior width CB-cold | CB-warm |
|---|---|---|---|---|---|---|---|
| 300 | 0.029 | 0.106 | 0.038 | +0.078 | +0.006 | 1.166 | 1.489 |
| 500 | 0.022 | 0.089 | 0.034 | +0.070 | +0.008 | 1.097 | 1.312 |
| 700 | 0.018 | 0.080 | 0.028 | +0.058 | +0.009 | 1.040 | 1.211 |
| 900 | 0.019 | 0.068 | 0.024 | +0.048 | +0.009 | 1.075 | 1.210 |
| 1100 | 0.018 | 0.060 | 0.031 | +0.045 | +0.008 | 0.989 | 1.142 |
| 1300 | 0.020 | 0.074 | 0.032 | +0.053 | +0.011 | 1.029 | 1.126 |
| 1500 | 0.025 | 0.073 | 0.029 | +0.052 | +0.013 | 0.998 | 1.108 |
| 1700 | 0.018 | 0.062 | 0.034 | +0.045 | +0.014 | 0.977 | 1.079 |
| 1900 | 0.017 | 0.064 | 0.033 | +0.048 | +0.008 | 0.913 | 1.077 |

Windowed PDR (100 s windows, pooled over the 10 seeds): first window start from which two consecutive windows of CB are within 0.02 of FW's:

- CB-cold: 450 s (= 150 s after the data start); mean windowed difference to FW over the run -0.026
- CB-warm: 300 s (= 0 s after the data start); mean windowed difference to FW over the run -0.001

## Regret over time, N = 300, 30 sources (mean per decision of the 10 seeds; regret in utility units = success x progress)

| window start (s) | FW vs oracle | CB-cold vs oracle | CB-warm vs oracle | CB-cold vs FW | CB-warm vs FW | mean posterior width CB-cold | CB-warm |
|---|---|---|---|---|---|---|---|
| 300 | 0.029 | 0.094 | 0.037 | +0.070 | +0.008 | 0.963 | 1.279 |
| 500 | 0.029 | 0.083 | 0.038 | +0.062 | +0.014 | 0.896 | 1.131 |
| 700 | 0.027 | 0.075 | 0.045 | +0.055 | +0.019 | 0.856 | 1.025 |
| 900 | 0.025 | 0.070 | 0.040 | +0.050 | +0.019 | 0.835 | 0.985 |
| 1100 | 0.026 | 0.069 | 0.045 | +0.045 | +0.022 | 0.787 | 0.919 |
| 1300 | 0.025 | 0.073 | 0.050 | +0.054 | +0.030 | 0.803 | 0.919 |
| 1500 | 0.022 | 0.067 | 0.054 | +0.043 | +0.032 | 0.788 | 0.847 |
| 1700 | 0.026 | 0.077 | 0.049 | +0.054 | +0.025 | 0.809 | 0.865 |
| 1900 | 0.022 | 0.079 | 0.047 | +0.055 | +0.022 | 0.806 | 0.898 |

Windowed PDR (100 s windows, pooled over the 10 seeds): first window start from which two consecutive windows of CB are within 0.02 of FW's:

- CB-cold: 1100 s (= 800 s after the data start); mean windowed difference to FW over the run -0.026
- CB-warm: 300 s (= 0 s after the data start); mean windowed difference to FW over the run -0.006
