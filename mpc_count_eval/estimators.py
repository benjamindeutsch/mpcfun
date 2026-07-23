"""Two estimators for k = |Sol(phi1) AND Sol(phi2)|:

1. `local_enumeration_construction`: the two-party construction where party 1
   finds a saturating hash dimension i1 for phi1 alone, enumerates its (small)
   solution set inside that cell, and evaluates those assignments against
   phi2 (no access to phi2's structure beyond membership evaluation).

2. `standard_approxmc`: the textbook single-formula ApproxMC-style baseline,
   applied directly to the conjunction phi1 AND phi2, ignoring the two-party
   split. Used purely as a reference for "what quality is achievable if you
   could hash the conjunction directly."

Both use the same exact brute-force machinery (bruteforce.py, hashing.py)
since n is small; "solving" is just boolean-array indexing.
"""
import math

import numpy as np

from hashing import sample_hash, cell_mask


def _search_saturating_i(sol, n, thresh, rng, i_max=None):
    """Smallest i such that |Sol ∩ Cell(h_i)| < thresh, via fresh (A,b) at
    each candidate i (each draw is exact, using the full brute-force solution
    mask). Returns (i, count_at_i, mask_at_i)."""
    if i_max is None:
        i_max = n + 4
    for i in range(0, i_max + 1):
        A, b = sample_hash(i, n, rng)
        mask = cell_mask(A, b, n)
        count = int(np.count_nonzero(sol & mask))
        if count < thresh:
            return i, count, mask
    # Fell through without saturating (only for pathological thresh<=0 or
    # near-tautological formulas); just use the last i tried.
    return i, count, mask


def find_i1(sol1, n, thresh, rng, i_max=None):
    i1, _, _ = _search_saturating_i(sol1, n, thresh, rng, i_max=i_max)
    return i1


def local_enumeration_construction(sol1, sol2, n, thresh, itercount, rng, delta=0.05):
    """Run the local-enumeration-plus-evaluation construction.

    Returns a dict with:
      - i1: the saturating dimension found for phi1
      - S1: |Sol(phi1)|
      - k_hats: list of per-repetition k_hat values (x' * 2^i1)
      - xs: list of per-repetition x' values
      - mean_k_hat, median_k_hat: the two distinctly-reported summaries
      - chernoff_ubs: list (nan unless x'==0 that rep) of the zero-hits
        Chernoff-style bound 2^i1 * ln(1/delta)
      - cheby_ub: the single static Chebyshev/Markov zero-hits bound
        S1 / (thresh * delta) (independent of any one repetition)
      - markov_lbs: list (nan unless x'>0 that rep) of delta * x' * 2^i1
    """
    i1 = find_i1(sol1, n, thresh, rng)

    k_hats = []
    xs = []
    chernoff_ubs = []
    markov_lbs = []
    for _ in range(itercount):
        A, b = sample_hash(i1, n, rng)
        mask = cell_mask(A, b, n)
        enumerated = sol1 & mask  # party 1's local enumeration of Sol(phi1) ∩ Cell
        x_prime = int(np.count_nonzero(enumerated & sol2))
        k_hat = x_prime * (2 ** i1)

        k_hats.append(k_hat)
        xs.append(x_prime)
        if x_prime == 0:
            chernoff_ubs.append((2 ** i1) * math.log(1.0 / delta))
            markov_lbs.append(float("nan"))
        else:
            chernoff_ubs.append(float("nan"))
            markov_lbs.append(delta * x_prime * (2 ** i1))

    S1 = int(np.count_nonzero(sol1))
    cheby_ub = S1 / (thresh * delta)

    k_hats = np.array(k_hats, dtype=float)
    return {
        "i1": i1,
        "S1": S1,
        "k_hats": k_hats,
        "xs": np.array(xs, dtype=int),
        "mean_k_hat": float(np.mean(k_hats)),
        "median_k_hat": float(np.median(k_hats)),
        "chernoff_ubs": np.array(chernoff_ubs, dtype=float),
        "cheby_ub": cheby_ub,
        "markov_lbs": np.array(markov_lbs, dtype=float),
        "delta": delta,
    }


def standard_approxmc(sol_and, n, thresh, itercount, rng):
    """Baseline: standard hash-based approximate counter (ApproxMC Algorithm 1
    style) applied directly to the conjunction phi1 AND phi2.

    Each of `itercount` trials independently searches, from scratch (fresh
    (A,b) at every candidate dimension), for the smallest i at which
    |Sol(phi1 AND phi2) ∩ Cell(h_i)| < thresh, then forms
    trial_estimate = count * 2**i. The median across trials is the
    recommended point estimate (trials share a common healthy mu by
    construction, since i is searched to match k's own scale each time).
    """
    trial_estimates = []
    trial_is = []
    trial_counts = []
    for _ in range(itercount):
        i, count, _ = _search_saturating_i(sol_and, n, thresh, rng)
        trial_estimates.append(count * (2 ** i))
        trial_is.append(i)
        trial_counts.append(count)

    trial_estimates = np.array(trial_estimates, dtype=float)
    return {
        "k_hats": trial_estimates,
        "is_": np.array(trial_is, dtype=int),
        "counts": np.array(trial_counts, dtype=int),
        "median_k_hat": float(np.median(trial_estimates)),
        "mean_k_hat": float(np.mean(trial_estimates)),
    }
