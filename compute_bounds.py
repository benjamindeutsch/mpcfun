import math

N = 128

def i_lb(s1, s2, n=N, thresh=1.0):
    numerator = max(0, s1 + s2 - (1<<n))
    if numerator == 0:
        return 0
    return math.ceil(math.log2(numerator / thresh))

def i_ub(s1, s2, thresh=1.0):
    numerator = min(s1, s2)
    if numerator <= 0:
        return -math.inf
    return max(0, math.ceil(math.log2(numerator / thresh)))

def report(S1, S2, thresh, n=N):
    lb, ub = i_lb(S1, S2, n, thresh), i_ub(S1, S2, thresh)
    print(f"n={n}, thresh={thresh}, S1={S1:.3e}, S2={S2:.3e} "
          f"-> [i_LB, i_UB] = [{lb}, {ub}]  width={ub-lb}")

# Regime 1: sparse (your original intent) — i_LB will clip to 0, bracket = i_UB only
report(1<<61, 1<<32, thresh=1.0)
report(1<<61, 1^32, thresh=100.0)

# Regime 2: dense enough to trigger the Bonferroni term — S1+S2 > 2^N
report(round(0.6 * (1<<N)), round(0.6 * (1<<N)), thresh=1.0)      # sum = 1.2*2^N > 2^N
report(round(0.6 * (1<<N)), round(0.6 * (1<<N)), thresh=100.0)

# Regime 3: boundary sweep, symmetric densities crossing rho1+rho2=1
for rho in [0.3, 0.45, 0.5, 0.55, 0.7]:
    report(round(rho * (1<<N)), round(rho * (1<<N)), thresh=1.0)