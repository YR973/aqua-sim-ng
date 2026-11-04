#!/usr/bin/env python3
"""Fit the A1 weights offline (spec §11 A1): L2 logistic regression of hop success on the 9 CB features.

Usage: fit_a1.py OUT_CONFIG decisions1.csv [decisions2.csv ...]
Each decisions.csv comes from a round-robin-with-feedback run on TRAINING seeds only (101-110); the script refuses
file names that look like test seeds (1-20 as '_<seed>' suffix). Only rows of head decisions (learn == 1) are used.
The regulariser is the same prior precision as the learner (lambda0 = 1). Writes a config file with the line
    SelectorAttributes = Weights=w1,...,w9
and prints the weights, log-loss and a held-out-free summary. Plain numpy (Newton's method), no sklearn needed.
"""
import re
import sys

import numpy as np
import pandas as pd

LAMBDA0 = 1.0   # params::learner::kLambda0


def load(paths):
    frames = []
    for p in paths:
        m = re.search(r"_(\d+)[/\\]", p)
        if m and int(m.group(1)) <= 20:
            sys.exit(f"refusing {p}: looks like a TEST seed (1-20); A1 may only be fitted on training seeds 101-110")
        d = pd.read_csv(p)
        frames.append(d[d.learn == 1])
    d = pd.concat(frames, ignore_index=True)
    X = d[[f"x{i}" for i in range(1, 10)]].to_numpy(dtype=float)   # x9 is the intercept column (all ones)
    y = d["label"].to_numpy(dtype=float)
    if not np.any(X[:, :8] != 0.0) or not np.all(X[:, 8] == 1.0):
        sys.exit("the decision logs contain no features (x1..x8 all zero / x9 not 1): re-run with a selector that logs them")
    return X, y


def sigmoid(z):
    return 1.0 / (1.0 + np.exp(-np.clip(z, -30, 30)))


def fit(X, y, lam=LAMBDA0, iters=50):
    w = np.zeros(X.shape[1])
    for _ in range(iters):
        p = sigmoid(X @ w)
        grad = X.T @ (p - y) + lam * w
        H = (X * (p * (1 - p))[:, None]).T @ X + lam * np.eye(X.shape[1])
        step = np.linalg.solve(H, grad)
        w -= step
        if np.abs(step).max() < 1e-9:
            break
    return w


def logloss(X, y, w):
    p = np.clip(sigmoid(X @ w), 1e-12, 1 - 1e-12)
    return -np.mean(y * np.log(p) + (1 - y) * np.log(1 - p))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out, paths = sys.argv[1], sys.argv[2:]
    X, y = load(paths)
    w = fit(X, y)
    base = y.mean()
    constant_loss = -np.mean(y * np.log(base) + (1 - y) * np.log(1 - base))
    print(f"rows={len(y)}  positive rate={base:.3f}  log-loss fitted={logloss(X, y, w):.4f}  constant model={constant_loss:.4f}")
    names = ["distance", "progress", "depth", "battery", "age", "queue", "closing", "margin", "intercept"]
    for n, v in zip(names, w):
        print(f"  w_{n:<9s} = {v:+.4f}")
    weights = ",".join(f"{v:.6f}" for v in w)
    with open(out, "w") as f:
        f.write("# A1 weights fitted by scripts/fit_a1.py on training seeds only; do not edit by hand.\n")
        f.write(f"# rows={len(y)} from {len(paths)} runs\n")
        f.write(f"SelectorAttributes = Weights={weights}\n")
    print("wrote", out)


if __name__ == "__main__":
    main()
