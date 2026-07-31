// karp_luby_estimate: dnf_weight * sum_{t=0}^{K-1} reciprocals[t] -- the
// raw (unnormalized) numerator of the Karp-Luby estimator for a DNF's
// true satisfying-assignment count, given K independent samples of
// (select_cube -> random_assignment -> count_satisfied_cubes ->
// divide_lookup).
//
// The unbiased estimate is (this result) / (K * lookup_scale<M>()): both
// K and the lookup scale are public compile-time constants, so a caller
// does that division for free in plaintext, on the *revealed* result,
// rather than spending circuit gates on it.
//
// Why this is an unbiased estimator (Karp-Luby / the "coverage
// algorithm" for counting a union of possibly-overlapping sets, here the
// sets of satisfying assignments of each cube): for a single trial,
// E[1/count] = true_count / dnf_weight (this is exactly what makes the
// algorithm work -- see gadgets/karp_luby/count_satisfied_cubes.h and
// gadgets/karp_luby/select_cube.h), so E[dnf_weight * sum_t(1/count_t)] =
// K * true_count. Averaging over K trials (dividing by K) and correcting
// divide_lookup's fixed-point SCALE recovers true_count, up to
// divide_lookup's own (tiny, fixed, M-independent) rounding error -- see
// its file comment.
//
// Ctx-generic (any BooleanContext), not tied to a specific session: the
// same code type-checks under ClearSession (plaintext, for fast gadget
// unit tests -- see tests/karp_luby/karp_luby_estimate_test.cpp) and
// SH2PCSession (the real 2PC protocol) unchanged.

#pragma once

#include "gadgets/circuit_cube.h"
#include "gadgets/karp_luby/divide_lookup.h"
#include "gadgets/common.h"

#include <cstdint>

namespace gadgets {

// karp_luby_trials(vars, epsilon, delta): the number of independent trials
// K a caller needs (see gadgets/karp_luby/select_cube.h/random_assignment.h/
// count_satisfied_cubes.h/divide_lookup.h and karp_luby_estimate() above)
// for the resulting Karp-Luby estimate to be within relative error epsilon
// of the true count with probability >= 1-delta:
//
//   K = ceil((1/delta) * (vars^2 - 1)^2 / epsilon^2)
//
// From Chebyshev: Pr[bad] <= Var/(epsilon^2 * mean^2) <= (vars^2-1)^2 /
// (K * epsilon^2) (the variance bound is exactly what makes the estimator
// work -- see the file comment above), so requiring that <= delta gives
// the K above. delta=1/4 (the constant folds to 4) was this function's
// first form, for probability >= 3/4; delta is now an explicit parameter
// so any target confidence -- e.g. ApproxMC's own defaults, epsilon=0.8,
// delta=0.2 (see tests/karp_luby/karp_luby_estimate_test.cpp and
// src/bench/bench_karp_luby.cpp) -- can be plugged in directly.
//
// A plain host-side compile-time calculation (no Ctx/wires involved,
// unlike everything else in this file) -- callers use it to pick K, e.g.
// `constexpr int K = karp_luby_trials(VARS, 0.1, 0.25);`, then pass that K
// on to karp_luby_estimate<Ctx,N,M,K> and every other K-templated gadget
// above.
constexpr int karp_luby_trials(int vars, double epsilon, double delta) {
    double m = (double)vars * (double)vars - 1.0;
    double k = (1.0 / delta) * m * m / (epsilon * epsilon);
    int ik = (int)k;
    return (k > (double)ik) ? ik + 1 : ik;  // ceiling
}

// Wide enough for the raw sum of K reciprocal-lookup terms, each at most
// 2^(kDivideLookupScaleBits+1) (see divide_lookup.h's DivideLookupResult):
// the same "N + bits_for(M)" pattern gadgets/circuit_cube.h's DnfWeight
// uses for summing M terms each up to 2^N, here summing K terms each up to
// 2^(kDivideLookupScaleBits+1). Deliberately computed as an addition
// (bits_for(K) + width), not bits_for(K * scale): K * lookup_scale<M>()
// can exceed INT_MAX well before K itself does, and emp::kernel::bits_for
// takes a plain int -- forming that product just to hand most of it to a
// narrowing cast is exactly the overflow this avoids.
template <BooleanContext Ctx, int M, int K>
using KarpLubySum = UInt_T<Ctx, (kDivideLookupScaleBits + 1) + bits_for(K)>;

// Wide enough for dnf_weight * KarpLubySum: the product of two values each
// bounded by 2^width fits in the sum of their widths.
template <BooleanContext Ctx, int N, int M, int K>
using KarpLubyEstimate = UInt_T<Ctx, (N + bits_for(M)) + (kDivideLookupScaleBits + 1) + bits_for(K)>;

template <BooleanContext Ctx, int N, int M, int K>
KarpLubyEstimate<Ctx, N, M, K> karp_luby_estimate(
        const DnfWeight<Ctx, N, M>& weight,
        const array<DivideLookupResult<Ctx, M>, (size_t)K>& reciprocals) {
    using Sum = KarpLubySum<Ctx, M, K>;
    using Result = KarpLubyEstimate<Ctx, N, M, K>;
    Ctx& ctx = *weight.context();

    Sum sum = Sum::constant(ctx, 0);
    for (size_t t = 0; t < (size_t)K; ++t)
        sum = sum + zext_to<Sum>(reciprocals[t]);

    return zext_to<Result>(weight) * zext_to<Result>(sum);
}

}  // namespace gadgets
