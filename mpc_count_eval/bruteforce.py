"""Exact brute-force solving over {0,1}^n via numpy bitmask enumeration.

n is assumed small (<=22 or so) so that materializing all 2^n assignments
is feasible. This gives exact ground truth (S1, S2, k) and is also used,
via the same assignment matrix, for exact hash-cell computation.
"""
from functools import lru_cache

import numpy as np


@lru_cache(maxsize=8)
def all_assignments(n):
    """Return an (2**n, n) uint8 array of all boolean assignments.

    Row i is the big-endian bit expansion of i: column 0 is the MSB-most
    variable in this internal ordering (the exact bit order doesn't matter
    as long as it's used consistently, which it is since this is the sole
    source of assignments used everywhere).
    """
    N = 1 << n
    idx = np.arange(N, dtype=np.uint32).reshape(-1, 1)
    shifts = np.arange(n - 1, -1, -1, dtype=np.uint32).reshape(1, -1)
    bits = (idx >> shifts) & 1
    return bits.astype(np.uint8)


def solve(clauses, n):
    """Return a boolean array of length 2**n: True where the CNF is satisfied."""
    X = all_assignments(n)
    sat = np.ones(X.shape[0], dtype=bool)
    for clause in clauses:
        clause_sat = np.zeros(X.shape[0], dtype=bool)
        for var, negated in clause:
            lit = X[:, var] == 1
            if negated:
                lit = ~lit
            clause_sat |= lit
        sat &= clause_sat
    return sat
