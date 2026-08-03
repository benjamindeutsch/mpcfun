// count_satisfied_cubes: the number of cubes in an array that a given
// assignment satisfies.
//
// Cube c is satisfied by assignment iff (assignment & c.mask) == c.bits --
// i.e. assignment agrees with c on every variable c constrains (variables
// c leaves free, where c.mask=0, never affect the check either way).
//
// A padding cube (bits=1^N, mask=0^N -- see gadgets/circuit_cube.h) is
// *never* satisfied by this check, with no special-casing needed:
// assignment & 0^N = 0^N, and 0^N != 1^N for any N > 0, so the equality
// always fails.
//
// Ctx-generic (any BooleanContext), not tied to a specific session: the
// same code type-checks under ClearSession (plaintext, for fast gadget
// unit tests -- see tests/karp_luby/count_satisfied_cubes_test.cpp) and
// SH2PCSession (the real 2PC protocol) unchanged.

#pragma once

#include "gadgets/circuit_cube.h"
#include "gadgets/common.h"

namespace gadgets {

// Wide enough to hold a count of 0..M satisfied cubes.
template <BooleanContext Ctx, int M>
using SatisfiedCount = UInt_T<Ctx, bits_for(M)>;

template <BooleanContext Ctx, int N, int M>
SatisfiedCount<Ctx, M> count_satisfied_cubes(const BitVec_T<Ctx, N>& assignment,
                                              const array<CircuitCube<Ctx, N>, (size_t)M>& cubes) {
    using Count = SatisfiedCount<Ctx, M>;
    Ctx& ctx = *assignment.context();

    Count count = Count::constant(ctx, 0);
    for (size_t i = 0; i < (size_t)M; ++i) {
        Bit_T<Ctx> satisfied = (assignment & cubes[i].mask) == cubes[i].bits;
        count = count + indicator<Count>(ctx, satisfied);
    }
    return count;
}

}  // namespace gadgets
