// divide_lookup: an exact fixed-point reciprocal 1/x for a secret x in
// [1,M], via an oblivious table lookup instead of a division circuit.
//
// table[x] = SCALE/x, where SCALE = lcm(1,2,...,M) -- divisible by every
// integer in [1,M], so every table entry is an exact integer (no rounding
// error). The table itself is PUBLIC constants (free -- no OT/garbling for
// those); only the O(M) equality comparisons that obliviously select
// table[index] cost real gates, the same linear-scan pattern
// gadgets/select_cube.h's cube_at_index uses to look up a cube by secret
// index. That's much cheaper than a real division circuit
// (kernel::div_full's restoring-division algorithm) for the small M this
// pipeline uses.
//
// lookup_scale<M>() is public API: callers un-scale a revealed result by
// dividing by it in plaintext (SCALE is a compile-time public constant,
// so that division costs nothing in the circuit) -- see
// gadgets/karp_luby_estimate.h.
//
// Ctx-generic (any BooleanContext), not tied to a specific session: the
// same code type-checks under ClearSession (plaintext, for fast gadget
// unit tests -- see tests/divide_lookup_test.cpp) and SH2PCSession (the
// real 2PC protocol) unchanged.

#pragma once

#include "gadgets/count_satisfied_cubes.h"
#include "emp-tool/circuits/typed.h"

#include <cstdint>

using emp::Bit_T;
using emp::UInt_T;
using emp::BooleanContext;

namespace gadgets {

constexpr uint64_t gcd_u64(uint64_t a, uint64_t b) { return b == 0 ? a : gcd_u64(b, a % b); }
constexpr uint64_t lcm_u64(uint64_t a, uint64_t b) { return a / gcd_u64(a, b) * b; }

// lcm(1, 2, ..., M).
template <int M>
constexpr uint64_t lookup_scale() {
    uint64_t s = 1;
    for (int i = 2; i <= M; ++i) s = lcm_u64(s, (uint64_t)i);
    return s;
}

// Wide enough to hold the largest table entry, SCALE/1 = SCALE.
template <BooleanContext Ctx, int M>
using DivideLookupResult = UInt_T<Ctx, bits_for((int)lookup_scale<M>())>;

// table[index-1] = SCALE/index, for a 1-indexed index in [1,M] -- matching
// gadgets/count_satisfied_cubes.h's SatisfiedCount, which is always >= 1
// (an assignment satisfies at least the cube it was built from).
template <BooleanContext Ctx, int M>
DivideLookupResult<Ctx, M> divide_lookup(const SatisfiedCount<Ctx, M>& index) {
    using Result = DivideLookupResult<Ctx, M>;
    constexpr uint64_t scale = lookup_scale<M>();
    Ctx& ctx = *index.context();

    Result result = Result::constant(ctx, scale / 1);
    for (int i = 2; i <= M; ++i) {
        Bit_T<Ctx> match = index == SatisfiedCount<Ctx, M>::constant(ctx, (uint64_t)i);
        result = result.select(match, Result::constant(ctx, scale / (uint64_t)i));
    }
    return result;
}

}  // namespace gadgets
