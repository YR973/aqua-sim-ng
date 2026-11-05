# Step 4e: robustness (3 seeds each)

| case | method | runs finished | non-finite decisions | non-finite theta nodes | Cholesky jitters | Cholesky resets (covariance not positive definite) | max abs theta | max trace(A) | PDR | peak pending attempts |
|---|---|---|---|---|---|---|---|---|---|---|
| N300, 45 sources | CB-cold | 3/3 | 0 | 0 | 0 | 0 | 1.31 | 73.1 | 0.438 | 86 |
| N300, 45 sources | CB-warm | 3/3 | 0 | 0 | 0 | 0 | 9.41 | 43.8 | 0.430 | 87 |
| N300, 45 sources | CB-warm+TD | 3/3 | 0 | 0 | 0 | 0 | 9.30 | 61.6 | 0.409 | 65 |
| N150, 10 sources | CB-cold | 3/3 | 0 | 0 | 0 | 0 | 1.09 | 42.3 | 0.806 | 37 |
| N150, 10 sources | CB-warm | 3/3 | 0 | 0 | 0 | 0 | 9.43 | 35.7 | 0.796 | 49 |
| N150, 10 sources | CB-warm+TD | 3/3 | 0 | 0 | 0 | 0 | 9.27 | 44.8 | 0.838 | 41 |
| N150, 45 sources | CB-cold | 3/3 | 0 | 0 | 0 | 0 | 1.97 | 88.2 | 0.484 | 88 |
| N150, 45 sources | CB-warm | 3/3 | 0 | 0 | 0 | 0 | 9.40 | 75.1 | 0.494 | 90 |
| N150, 45 sources | CB-warm+TD | 3/3 | 0 | 0 | 0 | 0 | 9.39 | 69.4 | 0.498 | 75 |
