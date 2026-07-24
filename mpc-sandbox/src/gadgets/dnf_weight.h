// dnf_weight: sums an array of per-cube weights (see gadgets/cube_weight.h,
// gadgets/circuit_cube.h for CubeWeight) into a single total -- the DNF's
// overall satisfying-assignment count, when its cubes are pairwise disjoint
// (which is what a correctly-built cube cover gives you: padding cubes
// contribute weight 0 via cube_weight(), so they add nothing here either).
//
// Ctx-generic (any BooleanContext), not tied to a specific session: the
// same code type-checks under ClearSession (plaintext, for fast gadget
// unit tests -- see tests/dnf_weight_test.cpp) and SH2PCSession (the real
// 2PC protocol) unchanged.

#pragma once

#include "gadgets/circuit_cube.h"
#include "emp-tool/circuits/typed.h"
#include "emp-tool/circuits/numeric_kernels.h"

#include <array>
#include <cstddef>

using std::array;
using std::size_t;
using emp::BooleanContext;
using emp::UInt_T;
using emp::kernel::bits_for;

namespace gadgets {

// Wide enough to hold the largest possible sum of M CubeWeight<Ctx,N> terms
// (each up to 2^N): M * 2^N < 2^(N + bits_for(M)), so N + bits_for(M) bits
// always suffice, regardless of how large N itself is (bits_for(M) depends
// only on the compile-time cube count M, not on 2^N).
template <BooleanContext Ctx, int N, int M>
using DnfWeight = UInt_T<Ctx, N + bits_for(M)>;

template <BooleanContext Ctx, int N, int M>
DnfWeight<Ctx, N, M> dnf_weight(const array<CubeWeight<Ctx, N>, (size_t)M>& weights) {
    constexpr int W = N + bits_for(M);
    DnfWeight<Ctx, N, M> total = weights[0].template zext<W>();
    for (size_t i = 1; i < (size_t)M; ++i)
        total = total + weights[i].template zext<W>();
    return total;
}

}  // namespace gadgets
