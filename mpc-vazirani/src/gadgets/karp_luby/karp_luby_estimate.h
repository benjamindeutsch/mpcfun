// karp_luby_estimate: dnf_weight * (number of trials whose sampled cube
// was the canonical one) -- the raw (unnormalized) numerator of the
// Karp-Luby coverage-algorithm estimator for a DNF's true satisfying-
// assignment count, given K independent trials of (select_cube ->
// random_assignment -> is_canonical_sample).
//
// The unbiased estimate is (this result) / K: K is a public compile-time
// constant, so a caller does that division for free in plaintext on the
// *revealed* result. Unlike gadgets/vazirani/vazirani_estimate.h's
// division-by-count-via-lookup-table, there's no fixed-point scale to
// carry here at all -- each trial's indicator is an exact {0,1} bit, not
// an approximate reciprocal, so this estimator has no per-trial rounding
// error.
//
// Why this is an unbiased estimator: for a single trial, letting mu =
// Pr[sampled cube is canonical] = true_count / dnf_weight (the standard
// Karp-Luby argument -- each satisfying assignment of the union is counted
// by exactly one trial outcome, its lowest-indexed covering cube), summing
// K i.i.d. {0,1} indicators and multiplying by dnf_weight gives
// E[dnf_weight * sum_t(Y_t)] = K * true_count.

#pragma once

#include "gadgets/circuit_cube.h"
#include "gadgets/common.h"
#include "gadgets/general/trial_count.h"

namespace gadgets {

// K-trials bound: identical to gadgets/vazirani/vazirani_estimate.h's
// vazirani_trials (see gadgets/general/trial_count.h's coverage_trials for
// the shared derivation) -- Karp-Luby's Y_t is a {0,1} canonical-sample
// indicator rather than Vazirani's 1/count reciprocal, but both are
// unbiased single-trial estimators bounded in [0,1] with mean mu =
// true_count/dnf_weight, so the same Var(Y_t)/mu^2 <= 1/mu - 1 <= N-1
// structure (N = number of cubes in the conjoined DNF) gives the same K.
constexpr int karp_luby_trials(int cubes, double epsilon, double delta) {
    return coverage_trials(cubes, epsilon, delta);
}

// Wide enough to hold a count of 0..K canonical trials.
template <BooleanContext Ctx, int K>
using GoodCount = UInt_T<Ctx, bits_for(K + 1)>;

// Wide enough for dnf_weight * GoodCount: the product of two values each
// bounded by 2^width fits in the sum of their widths. Narrower than
// gadgets/vazirani/vazirani_estimate.h's VaziraniEstimate for the same
// N/M/K, since there's no fixed-point lookup-scale term to carry.
template <BooleanContext Ctx, int N, int M, int K>
using KarpLubyEstimate = UInt_T<Ctx, (N + bits_for(M)) + bits_for(K + 1)>;

// Mechanical: sum the K {0,1} indicators (each trial's
// gadgets::is_canonical_sample() result) into a GoodCount, then multiply
// by the total weight -- the same zext-to-common-width-then-multiply/sum
// pattern every estimator in this project uses (see vazirani_estimate.h).
template <BooleanContext Ctx, int N, int M, int K>
KarpLubyEstimate<Ctx, N, M, K> karp_luby_estimate(
        const DnfWeight<Ctx, N, M>& weight,
        const array<Bit_T<Ctx>, (size_t)K>& indicators) {
    using Count = GoodCount<Ctx, K>;
    using Result = KarpLubyEstimate<Ctx, N, M, K>;
    Ctx& ctx = *weight.context();

    Count good = Count::constant(ctx, 0);
    for (size_t t = 0; t < (size_t)K; ++t)
        good = good + indicator<Count>(ctx, indicators[t]);

    return zext_to<Result>(weight) * zext_to<Result>(good);
}

}  // namespace gadgets
