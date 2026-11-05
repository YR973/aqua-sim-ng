# Counterfactual analysis (hop ACK + retries, Csma, calibrated channel, seeds 101-120)

## 1. What happens to the DATA frame sent to the chosen forwarder (share of routed attempts, mean of seeds)

| N | sources | method | PDR | attempts/run | ok | per_loss | collision_with_data | collision_with_beacon_hello | collision_with_ack_nack | half_duplex | receiver_dead | out_of_range | never_on_air | unexplained_phy_busy |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 100 | 10 | FW | 0.821 | 3895 | 0.654 | 0.194 | 0.080 | 0.026 | 0.026 | 0.008 | 0.001 | 0.007 | 0.000 | 0.004 |
| 100 | 10 | CB | 0.796 | 4056 | 0.605 | 0.253 | 0.076 | 0.024 | 0.025 | 0.004 | 0.000 | 0.010 | 0.000 | 0.004 |
| 100 | 30 | FW | 0.628 | 10808 | 0.486 | 0.161 | 0.213 | 0.018 | 0.040 | 0.014 | 0.052 | 0.006 | 0.000 | 0.011 |
| 100 | 30 | CB | 0.612 | 11023 | 0.470 | 0.210 | 0.198 | 0.019 | 0.039 | 0.010 | 0.037 | 0.007 | 0.000 | 0.010 |
| 300 | 10 | FW | 0.768 | 3777 | 0.593 | 0.214 | 0.099 | 0.046 | 0.026 | 0.006 | 0.000 | 0.010 | 0.000 | 0.006 |
| 300 | 10 | CB | 0.739 | 3996 | 0.547 | 0.280 | 0.088 | 0.043 | 0.022 | 0.003 | 0.000 | 0.011 | 0.000 | 0.005 |
| 300 | 30 | FW | 0.596 | 9912 | 0.460 | 0.184 | 0.231 | 0.037 | 0.040 | 0.009 | 0.018 | 0.009 | 0.000 | 0.013 |
| 300 | 30 | CB | 0.567 | 9886 | 0.434 | 0.250 | 0.206 | 0.037 | 0.035 | 0.007 | 0.007 | 0.011 | 0.000 | 0.012 |

## 2. Learnable ceiling for the forwarder choice (second half of the window; hop success per DATA transmission, mean +- 95% CI over seeds)

Time-split oracle: for every (sender, candidate) the success rate *including collisions* in the first half (Laplace 1/2 prior), then argmax over the candidates of each second-half decision. Hindsight = some candidate would have received the frame (upper bound, uses the outcome). 'info' = share of second-half decisions where every candidate had >= 2 first-half samples.

| N | sources | method | decisions/run (2nd half) | chosen | FW shadow | time-split oracle | hindsight best | oracle - chosen | info |
|---|---|---|---|---|---|---|---|---|---|
| 100 | 10 | FW | 1903 | 0.643 +- 0.010 | 0.643 +- 0.010 | 0.600 +- 0.015 | 0.786 +- 0.013 | -0.043 +- 0.008 | 0.43 |
| 100 | 10 | CB | 1967 | 0.595 +- 0.013 | 0.641 +- 0.011 | 0.611 +- 0.012 | 0.783 +- 0.014 | +0.016 +- 0.009 | 0.46 |
| 100 | 30 | FW | 3075 | 0.522 +- 0.016 | 0.522 +- 0.016 | 0.513 +- 0.016 | 0.666 +- 0.019 | -0.009 +- 0.009 | 0.54 |
| 100 | 30 | CB | 3236 | 0.504 +- 0.017 | 0.493 +- 0.018 | 0.490 +- 0.018 | 0.645 +- 0.022 | -0.014 +- 0.006 | 0.56 |
| 300 | 10 | FW | 1839 | 0.587 +- 0.013 | 0.587 +- 0.013 | 0.535 +- 0.017 | 0.722 +- 0.014 | -0.052 +- 0.009 | 0.29 |
| 300 | 10 | CB | 1977 | 0.551 +- 0.014 | 0.609 +- 0.014 | 0.552 +- 0.015 | 0.748 +- 0.016 | +0.001 +- 0.005 | 0.27 |
| 300 | 30 | FW | 3069 | 0.519 +- 0.015 | 0.519 +- 0.015 | 0.483 +- 0.014 | 0.679 +- 0.014 | -0.036 +- 0.008 | 0.36 |
| 300 | 30 | CB | 3034 | 0.505 +- 0.016 | 0.535 +- 0.014 | 0.498 +- 0.016 | 0.694 +- 0.016 | -0.007 +- 0.008 | 0.36 |

## 3. CB against the FW shadow at the same decisions (second half)

| N | sources | decisions where CB != FW | hop success of CB's choice there | hop success FW's choice would have had | loss causes of CB's choice there |
|---|---|---|---|---|---|
| 100 | 10 | 0.319 +- 0.022 | 0.582 | 0.728 | ok 0.58, per_loss 0.27, collision_with_data 0.08, collision_with_ack_nack 0.03 |
| 100 | 30 | 0.321 +- 0.023 | 0.544 | 0.513 | ok 0.54, per_loss 0.23, collision_with_data 0.11, receiver_dead 0.06 |
| 300 | 10 | 0.371 +- 0.025 | 0.539 | 0.698 | ok 0.54, per_loss 0.27, collision_with_data 0.09, collision_with_beacon_hello 0.05 |
| 300 | 30 | 0.362 +- 0.013 | 0.505 | 0.586 | ok 0.50, per_loss 0.27, collision_with_data 0.11, collision_with_beacon_hello 0.04 |
