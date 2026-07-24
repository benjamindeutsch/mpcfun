// Circuit gadget computing a DNF cube's "weight": the number of full
// assignments over VARS (=N) variables that satisfy it.
//
// A cube fixes r = popcount(mask) of the VARS variables and leaves the
// other VARS-r free, so it is satisfied by exactly 2^(VARS-r) of the 2^VARS
// possible assignments -- the standard cube-weight formula for counting a
// DNF's satisfying assignments from its (disjoint) cubes.
//
// Ctx-generic like gadgets/circuit_cube.h, for the same reason: testable
// under ClearSession (see tests/cube_weight_test.cpp) with zero
// changes needed to later run under the real SH2PCSession protocol.

#pragma once

#include "gadgets/circuit_cube.h"
#include "emp-tool/circuits/typed.h"

#include <array>
#include <cstddef>
#include <cstdint>

using std::array;
using std::size_t;
using emp::BitVec_T;
using emp::UInt_T;
using emp::BooleanContext;

namespace gadgets {

// 2^(N - r), r = popcount(c.mask) -- except a padding cube (c.pad) always
// gets weight 0: it isn't a real disjunct, so it must not contribute to a
// sum of weights across a DNF (mask=0^N would otherwise give it 2^N, the
// same weight as a genuine empty/always-true cube, which is wrong).
template <BooleanContext Ctx, int N>
CubeWeight<Ctx, N> cube_weight(const CircuitCube<Ctx, N>& c) {
    using W = CubeWeight<Ctx, N>;
    Ctx& ctx = *c.mask.context();

    W r = c.mask.as_uint().template popcount<N + 1>();
    W vars = W::constant(ctx, (uint64_t)N);
    W shift = vars - r;
    W weight = W::constant(ctx, 1) << shift;

    return weight.select(c.pad, W::constant(ctx, 0));  // pad ? 0 : weight
}

// M is generic -- callers pass CUBES cubes straight from a parsed DNF, or
// CUBES*CUBES results from conjoin_dnf(), or any other fixed count.
template <BooleanContext Ctx, int N, int M>
array<CubeWeight<Ctx, N>, (size_t)M> cube_weights(const array<CircuitCube<Ctx, N>, (size_t)M>& cubes) {
    array<CubeWeight<Ctx, N>, (size_t)M> out{};
    for (size_t i = 0; i < (size_t)M; ++i) out[i] = cube_weight<Ctx, N>(cubes[i]);
    return out;
}

}  // namespace gadgets
