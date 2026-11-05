# Step C: headroom per scenario (N = 300, Csma, 10 seeds 101-110, calibrated channel, ChannelAwareBeacons on for every row except FW0)

Scenarios: base; burst = rain squall (7.3 dB, 800-1400 s, central 2x2 cells, upper 1500 m); shipping = lane (+0.0013 dB, literature value); col10 = the +10 dB cell column.

| scenario | cell | PDR Oracle | PDR FW | PDR FW-NA | Oracle - FW | FW-NA - FW | time-split CF oracle - FW (hop success, whole run) | CF oracle - FW inside 800-1400 s | CF decisions in 800-1400 s per run |
|---|---|---|---|---|---|---|---|---|---|
| base | N=300, 10 src | 0.788 | 0.771 | 0.762 | +0.017 +- 0.020 | -0.009 +- 0.018 | -0.063 +- 0.011 | -0.049 +- 0.026 | 746 |
| base | N=300, 30 src | 0.620 | 0.598 | 0.601 | +0.021 +- 0.012 | +0.003 +- 0.011 | -0.035 +- 0.014 | -0.018 +- 0.013 | 1636 |
| burst | N=300, 10 src | 0.752 | 0.716 | 0.739 | +0.036 +- 0.016 | +0.022 +- 0.015 | -0.051 +- 0.014 | -0.039 +- 0.026 | 791 |
| burst | N=300, 30 src | 0.588 | 0.574 | 0.562 | +0.013 +- 0.021 | -0.012 +- 0.014 | -0.030 +- 0.011 | -0.027 +- 0.012 | 1726 |
| shipping | N=300, 10 src | 0.788 | 0.771 | 0.762 | +0.017 +- 0.020 | -0.009 +- 0.018 | -0.063 +- 0.011 | -0.049 +- 0.026 | 746 |
| shipping | N=300, 30 src | 0.620 | 0.599 | 0.601 | +0.020 +- 0.011 | +0.002 +- 0.011 | -0.034 +- 0.014 | -0.017 +- 0.012 | 1649 |
| col10 | N=300, 10 src | 0.465 | 0.437 | 0.433 | +0.028 +- 0.022 | -0.004 +- 0.033 | -0.041 +- 0.019 | -0.022 +- 0.023 | 828 |
| col10 | N=300, 30 src | 0.370 | 0.355 | 0.361 | +0.014 +- 0.011 | +0.006 +- 0.016 | -0.026 +- 0.009 | -0.014 +- 0.009 | 1790 |

## Beacon cost of the channel fields (base scenario; FW0 = without, FW = with)

| cell | beacon bytes per delivered packet FW0 | FW | beacon airtime share FW0 | FW | PDR FW0 | FW |
|---|---|---|---|---|---|---|
| N=300, 10 src | 158.3 | 161.6 | 0.1255 | 0.1324 | 0.751 | 0.771 |
| N=300, 30 src | 97.3 | 101.9 | 0.0582 | 0.0608 | 0.597 | 0.598 |
