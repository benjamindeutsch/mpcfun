// sample_cube: combines the two parties' random contributions into a joint
// uniform sample in [1, total_weight]. (More will move in here once it
// picks *which* cube that sample lands in, via the cube_intervals.h
// boundaries -- hence the name.)
//
// The random contributions themselves (drawing real entropy locally,
// feeding it in as private input) are the caller's job, same as any other
// private input in this codebase -- this gadget only does the wire-level
// combination, so it stays pure Ctx-generic math like every other gadget
// here: alice_r ^ bob_r is a free-XOR coin flip, uniform as long as at
// least one contribution is honestly random regardless of what the other
// party supplies. Reducing mod total lands in [0, total-1]; +1 shifts it
// up. Assumes total > 0 (the conjunction has at least one satisfiable
// cube) -- mod-by-zero otherwise.
//
// Ctx-generic (any BooleanContext), not tied to a specific session: the
// same code type-checks under ClearSession (plaintext, for fast gadget
// unit tests -- see tests/sample_cube_test.cpp) and SH2PCSession (the real
// 2PC protocol) unchanged.

#pragma once

#include "gadgets/circuit_cube.h"
#include "emp-tool/circuits/typed.h"

using emp::BitVec_T;
using emp::BooleanContext;

namespace gadgets {

// The bit width of each party's random contribution: matches
// DnfWeight<Ctx,N,M>'s own width, so the XOR result can be reinterpreted
// (zero gates) as a DnfWeight directly.
template <BooleanContext Ctx, int N, int M>
using SampleBits = BitVec_T<Ctx, N + bits_for(M)>;

template <BooleanContext Ctx, int N, int M>
DnfWeight<Ctx, N, M> sample_cube(const SampleBits<Ctx, N, M>& alice_r,
                                  const SampleBits<Ctx, N, M>& bob_r,
                                  const DnfWeight<Ctx, N, M>& total) {
    using W = DnfWeight<Ctx, N, M>;
    W joint_r = (alice_r ^ bob_r).as_uint();
    return (joint_r % total) + W::constant(*total.context(), 1);
}

}  // namespace gadgets
