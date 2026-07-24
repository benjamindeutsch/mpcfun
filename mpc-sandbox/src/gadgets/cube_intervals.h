// cube_intervals: given an M-element array of cube weights (see
// gadgets/cube_weight.h), computes the M+1 interval boundaries T_0..T_M:
// T_0 = 0, T_{i+1} = T_i + weights[i]. T_i is therefore the total weight of
// every cube before i -- the starting offset of cube i's own block in a
// running enumeration of satisfying assignments; T_M (the last boundary)
// is the total weight of all M cubes, matching gadgets/dnf_weight.h's
// result exactly.
//
// Ctx-generic (any BooleanContext), not tied to a specific session: the
// same code type-checks under ClearSession (plaintext, for fast gadget
// unit tests -- see tests/cube_intervals_test.cpp) and SH2PCSession (the
// real 2PC protocol) unchanged.

#pragma once

#include "gadgets/circuit_cube.h"
#include "emp-tool/circuits/typed.h"

#include <array>
#include <cstddef>

using std::array;
using std::size_t;
using emp::BooleanContext;

namespace gadgets {

template <BooleanContext Ctx, int N, int M>
array<DnfWeight<Ctx, N, M>, (size_t)M + 1> cube_intervals(const array<CubeWeight<Ctx, N>, (size_t)M>& weights) {
    using W = DnfWeight<Ctx, N, M>;
    constexpr int OutWidth = W::width();
    Ctx& ctx = *weights[0].context();

    array<W, (size_t)M + 1> intervals{};
    intervals[0] = W::constant(ctx, 0);
    for (size_t i = 0; i < (size_t)M; ++i)
        intervals[i + 1] = intervals[i] + weights[i].template zext<OutWidth>();
    return intervals;
}

}  // namespace gadgets
