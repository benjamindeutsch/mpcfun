// random_assignment: extends a cube's fixed values into a full random
// satisfying assignment over all N variables. Variables the cube
// constrains (mask=1) keep the cube's own value; variables it leaves free
// (mask=0) are filled in with the joint random bitstring r = alice_r ^
// bob_r -- the same free-XOR coin flip as gadgets/karp_luby/select_cube.h's
// sample_in_range (uniform as long as at least one contribution is
// honestly random, regardless of what the other party supplies), so if r
// is uniform, the result is a uniformly random assignment satisfying the
// cube.
//
//   assignment = bits | (~mask & r)
//
// On a constrained variable (mask=1): bits | (~1 & r) = bits | 0 = bits.
// On a free variable (mask=0, so bits=0 there by convention -- see
// gadgets/circuit_cube.h): bits | (~0 & r) = 0 | r = r.
//
// Ctx-generic (any BooleanContext), not tied to a specific session: the
// same code type-checks under ClearSession (plaintext, for fast gadget
// unit tests -- see tests/karp_luby/random_assignment_test.cpp) and
// SH2PCSession (the real 2PC protocol) unchanged.

#pragma once

#include "gadgets/circuit_cube.h"
#include "gadgets/common.h"

namespace gadgets {

template <BooleanContext Ctx, int N>
BitVec_T<Ctx, N> random_assignment(const CubeData<Ctx, N>& cube,
                                    const BitVec_T<Ctx, N>& alice_r,
                                    const BitVec_T<Ctx, N>& bob_r) {
    BitVec_T<Ctx, N> r = alice_r ^ bob_r;
    return cube.bits | (~cube.mask & r);
}

}  // namespace gadgets
