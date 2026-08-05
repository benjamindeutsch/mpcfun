// vazirani_estimate: dnf_weight * sum_{t=0}^{K-1} reciprocals[t] -- the
// raw (unnormalized) numerator of the Vazirani estimator for a DNF's
// true satisfying-assignment count, given K independent samples of
// (select_cube -> random_assignment -> count_satisfied_cubes ->
// divide_lookup).
//
// The unbiased estimate is (this result) / (K * lookup_scale<M>()): both
// K and the lookup scale are public compile-time constants, so a caller
// does that division for free in plaintext, on the *revealed* result,
// rather than spending circuit gates on it.
//
// Why this is an unbiased estimator (Vazirani / the "coverage
// algorithm" for counting a union of possibly-overlapping sets, here the
// sets of satisfying assignments of each cube): for a single trial,
// E[1/count] = true_count / dnf_weight (this is exactly what makes the
// algorithm work -- see gadgets/vazirani/count_satisfied_cubes.h and
// gadgets/general/select_cube.h), so E[dnf_weight * sum_t(1/count_t)] =
// K * true_count. Averaging over K trials (dividing by K) and correcting
// divide_lookup's fixed-point SCALE recovers true_count, up to
// divide_lookup's own (tiny, fixed, M-independent) rounding error -- see
// its file comment.
//
// Ctx-generic (any BooleanContext), not tied to a specific session: the
// same code type-checks under ClearSession (plaintext, for fast gadget
// unit tests -- see tests/vazirani/vazirani_estimate_test.cpp) and
// SH2PCSession (the real 2PC protocol) unchanged.

#pragma once

#include "gadgets/circuit_cube.h"
#include "gadgets/vazirani/divide_lookup.h"
#include "gadgets/general/trial_count.h"
#include "gadgets/common.h"

#include <cstdint>

namespace gadgets {

// K-trials bound: see gadgets/general/trial_count.h's coverage_trials for
// the derivation (shared verbatim with
// gadgets/karp_luby/karp_luby_estimate.h's karp_luby_trials -- both
// estimators are single-trial-in-[0,1] coverage algorithms with the same
// Var/mu^2 <= N-1 structure, so the same K works for both at a given
// epsilon/delta).
constexpr int vazirani_trials(int cubes, double epsilon, double delta) {
  return coverage_trials(cubes, epsilon, delta);
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
using VaziraniSum = UInt_T<Ctx, (kDivideLookupScaleBits + 1) + bits_for(K)>;

// Wide enough for dnf_weight * VaziraniSum: the product of two values each
// bounded by 2^width fits in the sum of their widths.
template <BooleanContext Ctx, int N, int M, int K>
using VaziraniEstimate = UInt_T<Ctx, (N + bits_for(M)) + (kDivideLookupScaleBits + 1) + bits_for(K)>;

template <BooleanContext Ctx, int N, int M, int K>
VaziraniEstimate<Ctx, N, M, K> vazirani_estimate(
        const DnfWeight<Ctx, N, M>& weight,
        const array<DivideLookupResult<Ctx, M>, (size_t)K>& reciprocals) {
    using Sum = VaziraniSum<Ctx, M, K>;
    using Result = VaziraniEstimate<Ctx, N, M, K>;
    Ctx& ctx = *weight.context();

    Sum sum = Sum::constant(ctx, 0);
    for (size_t t = 0; t < (size_t)K; ++t)
        sum = sum + zext_to<Sum>(reciprocals[t]);

    return zext_to<Result>(weight) * zext_to<Result>(sum);
}

}  // namespace gadgets
