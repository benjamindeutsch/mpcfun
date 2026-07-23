"""Random XOR hash-function sampling and exact cell computation.

h_i(x) = A x + b (mod 2), with A an (i x n) 0/1 matrix and b an (i,) 0/1
vector, BOTH drawn fresh and independently uniformly at random every time
a hash function is sampled. Cell(phi, h_i) is the set of solutions x of
phi with h_i(x) = 0, i.e. A x = b (mod 2) (since -b = b mod 2).

Fixing b=0 would force x=0^n into the checked cell whenever A happens to
map it there trivially (A @ 0 = 0 always satisfies Ax=0), i.e. 0^n would
land in the cell with probability 1 instead of 2^-i, breaking the
uniformity guarantee that every x lands in the cell with probability
exactly 2^-i independent of x. Since 0^n is just as likely as any other
assignment to be a solution of phi1/phi2, this would bias every trial.
Hence b must be freshly re-randomized alongside A on every draw.
"""
import numpy as np

from bruteforce import all_assignments


def sample_hash(i, n, rng):
    """Draw a fresh random (A, b) pair for an i-bit hash over n variables.

    rng: a numpy.random.Generator.
    """
    A = rng.integers(0, 2, size=(i, n), dtype=np.uint8)
    b = rng.integers(0, 2, size=(i,), dtype=np.uint8)
    return A, b


def cell_mask(A, b, n):
    """Boolean array of length 2**n: True where A x = b (mod 2)."""
    i = A.shape[0]
    if i == 0:
        # The empty hash always matches (the whole space is "the cell").
        return np.ones(1 << n, dtype=bool)
    X = all_assignments(n)  # (2**n, n) uint8
    prod = (X.astype(np.uint32) @ A.T.astype(np.uint32)) & 1  # (2**n, i)
    return np.all(prod == b.astype(np.uint32), axis=1)
