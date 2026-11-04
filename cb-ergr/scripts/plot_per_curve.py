#!/usr/bin/env python3
"""Plot measured vs analytic frame error rate from link-per-curve CSVs (milestone D2 check).

Usage: plot_per_curve.py OUT.png a.csv [b.csv ...]
The pilot (cb_ergr_pilot.py) was not available, so the reference curve is the analytic
AcousticLinkModel value written into each CSV (spec 4.4 formulas).
"""
import sys
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

out, files = sys.argv[1], sys.argv[2:]
fig, ax = plt.subplots(figsize=(6, 4))
for f in files:
    df = pd.read_csv(f)
    n = df["sent"].iloc[0]
    se = (df["per_analytic"] * (1 - df["per_analytic"]) / n) ** 0.5
    ax.plot(df["distance_m"], df["per_analytic"], "-", label=f"analytic ({f.split('/')[-1]})")
    ax.errorbar(df["distance_m"], df["per_measured"], yerr=1.96 * se, fmt="o", capsize=3,
                label=f"simulated ({f.split('/')[-1]})")
ax.set_xlabel("distance (m)")
ax.set_ylabel("frame error rate (150 B)")
ax.set_ylim(0, 1.02)
ax.grid(alpha=0.3)
ax.legend(fontsize=7)
fig.tight_layout()
fig.savefig(out, dpi=150)
print("wrote", out)
