// dnf_weight: sums an array of per-cube weights (see gadgets/general/cube_weight.h,
// gadgets/circuit_cube.h for CubeWeight) into a single total -- the DNF's
// overall satisfying-assignment count, when its cubes are pairwise disjoint
// (which is what a correctly-built cube cover gives you: padding cubes
// contribute weight 0 via cube_weight(), so they add nothing here either).
//
// Ctx-generic (any BooleanContext), not tied to a specific session: the
// same code type-checks under ClearSession (plaintext, for fast gadget
// unit tests -- see tests/general/dnf_weight_test.cpp) and SH2PCSession (the real
// 2PC protocol) unchanged.

#pragma once

#include "gadgets/circuit_cube.h"
#include "gadgets/common.h"

namespace gadgets {

template <BooleanContext Ctx, int N, int M>
DnfWeight<Ctx, N, M> dnf_weight(const array<CubeWeight<Ctx, N>, (size_t)M>& weights) {
    using W = DnfWeight<Ctx, N, M>;

    W total = zext_to<W>(weights[0]);
    for (size_t i = 1; i < (size_t)M; ++i)
        total = total + zext_to<W>(weights[i]);
    return total;
}

}  // namespace gadgets
