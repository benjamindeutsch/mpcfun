// divide_lookup: a fixed-point reciprocal 1/x for a secret x in [1,M], via
// an oblivious table lookup instead of a division circuit.
//
// table[x] = round(SCALE/x), where SCALE = 2^kDivideLookupScaleBits is a
// FIXED constant, independent of M. (An earlier version used SCALE =
// lcm(1,...,M) instead, for exact, rounding-free reciprocals -- fine at
// the small M=4 this pipeline first ran at, but it doesn't scale: by the
// prime number theorem, lcm(1..M)'s bit-length grows roughly linearly in
// M (lcm(1..42) alone already needs 58 bits), it overflows a uint64_t
// entirely around M=47, and even computed exactly via a bignum, every
// downstream reciprocal/sum/multiply would end up operating on
// thousands-of-bits-wide wires once M reaches the low thousands --
// unusable. A fixed power-of-two SCALE keeps every table entry's width
// constant regardless of M, at the cost of making reciprocals approximate
// instead of exact: relative rounding error is at most
// 2^-kDivideLookupScaleBits per entry, utterly negligible next to the
// Vazirani estimator's own O(1/sqrt(K)) sampling error at any K actually
// affordable to run -- see gadgets/vazirani/vazirani_estimate.h's
// vazirani_trials().)
//
// The table itself is PUBLIC constants (free -- no OT/garbling for
// those); only the O(M) equality comparisons that obliviously select
// table[index] cost real gates, the same linear-scan pattern
// gadgets/general/select_cube.h's cube_at_index uses to look up a cube
// by secret index. That's much cheaper than a real division circuit
// (kernel::div_full's restoring-division algorithm) for the M this
// pipeline uses.
//
// lookup_scale<M>() is public API: callers un-scale a revealed result by
// dividing by it in plaintext (SCALE is a compile-time public constant,
// so that division costs nothing in the circuit) -- see
// gadgets/vazirani/vazirani_estimate.h. Still templated on M for
// interface stability with existing callers, even though the value it
// returns no longer depends on M.
//
// Ctx-generic (any BooleanContext), not tied to a specific session: the
// same code type-checks under ClearSession (plaintext, for fast gadget
// unit tests -- see tests/vazirani/divide_lookup_test.cpp) and
// SH2PCSession (the real 2PC protocol) unchanged.

#pragma once

#include "gadgets/vazirani/count_satisfied_cubes.h"
#include "gadgets/common.h"

#include <cstdint>

namespace gadgets {

// 16 bits of fixed-point precision (relative error <= 2^-16 ~ 1.5e-5 per
// table entry): far beyond what matters next to Vazirani's own sampling
// error, and small enough to keep every downstream wire width modest even
// at large K (see vazirani_estimate.h's VaziraniSum/VaziraniEstimate,
// which add bits_for(K) on top of this rather than multiplying by it --
// see their comments for why that distinction matters).
constexpr int kDivideLookupScaleBits = 16;
constexpr uint64_t kDivideLookupScale = 1ull << kDivideLookupScaleBits;

template <int M>
constexpr uint64_t lookup_scale() {
    return kDivideLookupScale;
}

// Wide enough to hold the largest table entry: index=1 rounds exactly to
// SCALE itself (no rounding needed there), which needs
// kDivideLookupScaleBits+1 bits -- a power of two, same "N+1" reasoning as
// gadgets/circuit_cube.h's CubeWeight.
template <BooleanContext Ctx, int M>
using DivideLookupResult = UInt_T<Ctx, kDivideLookupScaleBits + 1>;

// table[index-1] = round(SCALE/index), for a 1-indexed index in [1,M] --
// matching gadgets/vazirani/count_satisfied_cubes.h's SatisfiedCount,
// which is always >= 1 (an assignment satisfies at least the cube it was
// built from). Rounds to nearest (not truncating) to minimize the
// per-entry relative error.
template <BooleanContext Ctx, int M>
DivideLookupResult<Ctx, M> divide_lookup(const SatisfiedCount<Ctx, M>& index) {
    using Result = DivideLookupResult<Ctx, M>;
    Ctx& ctx = *index.context();
    auto entry = [](uint64_t i) { return (kDivideLookupScale + i / 2) / i; };

    Result result = Result::constant(ctx, entry(1));
    for (int i = 2; i <= M; ++i) {
        Bit_T<Ctx> match = index == SatisfiedCount<Ctx, M>::constant(ctx, (uint64_t)i);
        result = result.select(match, Result::constant(ctx, entry((uint64_t)i)));
    }
    return result;
}

}  // namespace gadgets
