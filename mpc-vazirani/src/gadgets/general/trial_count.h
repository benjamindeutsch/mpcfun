// coverage_trials: the K-trials bound shared by every coverage-algorithm
// estimator in this project (gadgets/vazirani/vazirani_estimate.h's
// vazirani_trials, gadgets/karp_luby/karp_luby_estimate.h's
// karp_luby_trials). Both Vazirani's 1/count reciprocal and Karp-Luby's
// {0,1} canonical-sample indicator are single-trial estimators bounded in
// [0,1] with mean mu = true_count/dnf_weight, so the same
// Var(X_t)/mu^2 <= 1/mu - 1 <= N-1 concentration bound (N = number of
// cubes in the conjoined DNF) gives an identical K for both algorithms at
// the same epsilon/delta -- extracted here once both estimators needed the
// exact same formula, rather than duplicated per algorithm.
//
// Host-side arithmetic only (no wire types, no BooleanContext): used at
// compile time (constexpr) to size the K-templated per-algorithm circuits,
// not itself a circuit.

#pragma once

namespace gadgets {

// ln(x) for finite x > 0, usable in constant expressions (C++14).
constexpr double const_log(double x) {
    int e = 0;
    while (x >= 2.0) { x *= 0.5; ++e; }
    while (x <  1.0) { x *= 2.0; --e; }
    const double z = (x - 1.0) / (x + 1.0), z2 = z * z;   // |z| <= 1/3
    double term = z, sum = z;
    for (int k = 3; k < 40; k += 2) { term *= z2; sum += term / k; }
    return 2.0 * sum + e * 0.69314718055994530942;
}

// K = ceil( min( (1/delta), 3*ln(2/delta) ) * (S/W_max - 1) / epsilon^2 )
//
// With X_t in [0,1] and mu = E[X_t] = |U|/S (|U| = true satisfying-
// assignment count, S = dnf_weight):
//   Var(X_t) <= E[X_t^2] - mu^2 <= mu - mu^2   (since X_t <= 1)
// so Var/mu^2 <= 1/mu - 1 = S/|U| - 1 <= S/W_max - 1 <= N - 1,
// where N is the number of cubes in the CONJOINED dnf (cubes_a*cubes_b)
// and W_max is the largest single cube weight. The bound is linear in N,
// not quadratic -- that linearity IS the Vazirani/Karp-Luby coverage
// theorem.
//
// Chebyshev gives the 1/delta form; since the summands are i.i.d. in
// [0,1], multiplicative Chernoff gives the 3*ln(2/delta) form, which wins
// for delta < ~0.15. Take whichever is smaller.
constexpr int coverage_trials(double variance_ratio,  // S/W_max - 1, or N-1
                               double epsilon, double delta) {
    const double chebyshev = 1.0 / delta;
    const double chernoff  = 3.0 * const_log(2.0 / delta);
    const double c = (chebyshev < chernoff) ? chebyshev : chernoff;
    const double k = c * variance_ratio / (epsilon * epsilon);
    const int ik = (int)k;
    return (k > (double)ik) ? ik + 1 : ik;
}

// Conservative wrapper when S and W_max aren't available at compile time.
constexpr int coverage_trials(int cubes, double epsilon, double delta) {
    return coverage_trials((double)cubes * (double)cubes - 1.0, epsilon, delta);
}

}  // namespace gadgets
