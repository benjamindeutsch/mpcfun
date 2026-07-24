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
// divide_lookup's fixed-point SCALE recovers true_count.
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

// karp_luby_trials(vars, epsilon): the number of independent trials K a
// caller needs (see gadgets/karp_luby/select_cube.h/random_assignment.h/
// count_satisfied_cubes.h/divide_lookup.h and karp_luby_estimate() above)
// for the resulting Karp-Luby estimate to be within relative error epsilon
// of the true count with probability >= 3/4:
//
//   K = ceil(4 * (vars^2 - 1)^2 / epsilon^2)
//
// A plain host-side compile-time calculation (no Ctx/wires involved,
// unlike everything else in this file) -- callers use it to pick K, e.g.
// `constexpr int K = karp_luby_trials(VARS, 0.1);`, then pass that K on to
// karp_luby_estimate<Ctx,N,M,K> and every other K-templated gadget above.
constexpr int karp_luby_trials(int vars, double epsilon) {
    double m = (double)vars * (double)vars - 1.0;
    double k = 4.0 * m * m / (epsilon * epsilon);
    int ik = (int)k;
    return (k > (double)ik) ? ik + 1 : ik;  // ceiling
}

// Wide enough for the raw sum of K reciprocal-lookup terms, each at most
// lookup_scale<M>().
template <BooleanContext Ctx, int M, int K>
using KarpLubySum = UInt_T<Ctx, bits_for((int)((uint64_t)K * lookup_scale<M>()))>;

// Wide enough for dnf_weight * KarpLubySum: the product of two values each
// bounded by 2^width fits in the sum of their widths.
template <BooleanContext Ctx, int N, int M, int K>
using KarpLubyEstimate = UInt_T<Ctx, (N + bits_for(M)) + bits_for((int)((uint64_t)K * lookup_scale<M>()))>;

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
