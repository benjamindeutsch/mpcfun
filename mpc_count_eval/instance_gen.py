"""Random CNF instance-pair generator.

A clause is a tuple of literals; a literal is (var_index, negated_bool).
A CNF formula is a list of clauses (implicit AND of ORs).

phi2 is derived from phi1 with a "correlation knob" f in [0, 1]:
  - start from phi1's clause list, resized to m2 clauses (truncated if
    m2 <= m1, padded with fresh random clauses if m2 > m1)
  - replace a fraction f of those m2 clauses (chosen uniformly at random)
    with brand new random clauses

f=0 => phi2's clause set is exactly phi1's (resized) clause set.
f=1 => phi2 is fully independent of phi1 (every clause replaced).
"""
import random


def random_clause(n, w, rng):
    w = min(w, n)
    variables = rng.sample(range(n), w)
    return tuple((v, rng.random() < 0.5) for v in variables)


def random_cnf(n, m, w, rng):
    return [random_clause(n, w, rng) for _ in range(m)]


def make_instance_pair(n, m1, m2, w, f, rng=None, seed=None):
    """Generate (phi1, phi2) over n shared variables.

    Args:
        n: number of shared boolean variables.
        m1: number of clauses in phi1.
        m2: number of clauses in phi2.
        w: clause width (literals per clause).
        f: correlation knob in [0, 1]. f=0 -> phi2 shares phi1's clauses;
           f=1 -> phi2 fully independent random clauses.
        rng: an optional random.Random instance (preferred for reproducibility).
        seed: if rng is None, seed a fresh random.Random with this.

    Returns:
        (phi1, phi2) as lists of clauses.
    """
    if rng is None:
        rng = random.Random(seed)

    phi1 = random_cnf(n, m1, w, rng)

    base = list(phi1[:m2])
    if m2 > m1:
        base += random_cnf(n, m2 - m1, w, rng)

    n_replace = round(f * m2)
    replace_idx = set(rng.sample(range(m2), n_replace)) if n_replace > 0 else set()
    phi2 = [
        (random_clause(n, w, rng) if idx in replace_idx else clause)
        for idx, clause in enumerate(base)
    ]
    return phi1, phi2
