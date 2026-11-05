"""Small statistics helpers (no numpy/scipy on this machine): Student t cdf, paired CI, paired t test, TOST, Holm."""
import math


def _betacf(a, b, x):
    # continued fraction for the regularized incomplete beta (Numerical Recipes)
    MAXIT, EPS, FPMIN = 300, 3e-14, 1e-300
    qab, qap, qam = a + b, a + 1.0, a - 1.0
    c, d = 1.0, 1.0 - qab * x / qap
    d = FPMIN if abs(d) < FPMIN else d
    d = 1.0 / d
    h = d
    for m in range(1, MAXIT + 1):
        m2 = 2 * m
        aa = m * (b - m) * x / ((qam + m2) * (a + m2))
        d = 1.0 + aa * d
        d = FPMIN if abs(d) < FPMIN else d
        c = 1.0 + aa / c
        c = FPMIN if abs(c) < FPMIN else c
        d = 1.0 / d
        h *= d * c
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2))
        d = 1.0 + aa * d
        d = FPMIN if abs(d) < FPMIN else d
        c = 1.0 + aa / c
        c = FPMIN if abs(c) < FPMIN else c
        d = 1.0 / d
        de = d * c
        h *= de
        if abs(de - 1.0) < EPS:
            break
    return h


def betainc(a, b, x):
    if x <= 0.0:
        return 0.0
    if x >= 1.0:
        return 1.0
    bt = math.exp(math.lgamma(a + b) - math.lgamma(a) - math.lgamma(b) + a * math.log(x) + b * math.log(1.0 - x))
    if x < (a + 1.0) / (a + b + 2.0):
        return bt * _betacf(a, b, x) / a
    return 1.0 - bt * _betacf(b, a, 1.0 - x) / b


def t_cdf(t, df):
    x = df / (df + t * t)
    p = 0.5 * betainc(df / 2.0, 0.5, x)
    return 1.0 - p if t > 0 else p


def t_quantile(p, df):
    lo, hi = -50.0, 50.0
    for _ in range(200):
        mid = 0.5 * (lo + hi)
        if t_cdf(mid, df) < p:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi)


def mean_ci(xs, level=0.95):
    n = len(xs)
    m = sum(xs) / n
    if n < 2:
        return m, 0.0
    sd = math.sqrt(sum((x - m) ** 2 for x in xs) / (n - 1))
    return m, t_quantile(0.5 + level / 2.0, n - 1) * sd / math.sqrt(n)


def paired_t_p(diffs):
    """Two-sided p value of H0: mean difference = 0."""
    n = len(diffs)
    m = sum(diffs) / n
    sd = math.sqrt(sum((x - m) ** 2 for x in diffs) / (n - 1))
    if sd == 0.0:
        return 1.0 if m == 0.0 else 0.0
    t = m / (sd / math.sqrt(n))
    return 2.0 * (1.0 - t_cdf(abs(t), n - 1))


def tost_p(diffs, margin):
    """Two one-sided tests of H0: |mean difference| >= margin. Returns the larger one-sided p (equivalent if < alpha)."""
    n = len(diffs)
    m = sum(diffs) / n
    sd = math.sqrt(sum((x - m) ** 2 for x in diffs) / (n - 1))
    se = sd / math.sqrt(n) if sd > 0 else 1e-12
    p_lower = 1.0 - t_cdf((m + margin) / se, n - 1)   # H0: d <= -margin
    p_upper = t_cdf((m - margin) / se, n - 1)          # H0: d >= +margin
    return max(p_lower, p_upper)


def holm(pvals):
    """Holm-adjusted p values for a list (same order)."""
    m = len(pvals)
    order = sorted(range(m), key=lambda i: pvals[i])
    adj = [0.0] * m
    running = 0.0
    for rank, i in enumerate(order):
        running = max(running, min(1.0, (m - rank) * pvals[i]))
        adj[i] = running
    return adj


if __name__ == "__main__":
    # sanity: t_{0.975,19} = 2.093, P(T<=0) = 0.5
    assert abs(t_quantile(0.975, 19) - 2.093) < 1e-3
    assert abs(t_cdf(0.0, 10) - 0.5) < 1e-12
    print("ok", t_quantile(0.975, 19), t_quantile(0.95, 19))
