// select_cube: samples a joint random integer in [1, total_weight], finds
// which cube's block it falls in, and returns that cube's bits/mask --
// composed from three separately testable pieces:
//   1. sample_in_range(alice_r, bob_r, total) -- the joint random sample.
//   2. select_cube_index(z, intervals) -- which cube (1-indexed) it lands
//      in, via the gadgets/cube_intervals.h boundaries.
//   3. cube_at_index(index, cubes) -- an oblivious lookup of that cube's
//      bits/mask out of the full array.
//
// The random contributions themselves (drawing real entropy locally,
// feeding it in as private input) are the caller's job, same as any other
// private input in this codebase -- these gadgets only do the wire-level
// math, so they stay pure Ctx-generic like every other gadget here.
//
// Ctx-generic (any BooleanContext), not tied to a specific session: the
// same code type-checks under ClearSession (plaintext, for fast gadget
// unit tests -- see tests/select_cube_test.cpp) and SH2PCSession (the real
// 2PC protocol) unchanged.

#pragma once

#include "gadgets/circuit_cube.h"
#include "emp-tool/circuits/typed.h"

#include <array>
#include <cstddef>
#include <cstdint>

using std::array;
using std::size_t;
using emp::BitVec_T;
using emp::Bit_T;
using emp::UInt_T;
using emp::BooleanContext;

namespace gadgets {

// The bit width of each party's random contribution: matches
// DnfWeight<Ctx,N,M>'s own width, so the XOR result can be reinterpreted
// (zero gates) as a DnfWeight directly.
template <BooleanContext Ctx, int N, int M>
using SampleBits = BitVec_T<Ctx, N + bits_for(M)>;

// Joint uniform sample in [1, total]: alice_r ^ bob_r is a free-XOR coin
// flip, uniform as long as at least one contribution is honestly random
// regardless of what the other party supplies. Reducing mod total lands
// in [0, total-1]; +1 shifts it up. Assumes total > 0 (the conjunction has
// at least one satisfiable cube) -- mod-by-zero otherwise.
template <BooleanContext Ctx, int N, int M>
DnfWeight<Ctx, N, M> sample_in_range(const SampleBits<Ctx, N, M>& alice_r,
                                      const SampleBits<Ctx, N, M>& bob_r,
                                      const DnfWeight<Ctx, N, M>& total) {
    using W = DnfWeight<Ctx, N, M>;
    W joint_r = (alice_r ^ bob_r).as_uint();
    return (joint_r % total) + W::constant(*total.context(), 1);
}

// Wide enough to hold a count of 0..M+1 boundaries.
template <BooleanContext Ctx, int M>
using CubeIndex = UInt_T<Ctx, bits_for(M + 1)>;

// The unique 1-indexed j such that intervals[j-1] < z <= intervals[j] --
// i.e. which cube's block the sample z (from sample_in_range() above)
// falls in, counting cubes starting at 1 (cube 1 is intervals[0..1], cube
// M is intervals[M-1..M]).
//
// j = sum_{i=0}^{M} [z > intervals[i]]: intervals is non-decreasing, so
// the boundaries below z are exactly intervals[0..j-1] (j of them, since
// intervals[j-1] < z by definition, and intervals[j] >= z stops the run),
// so the count of "z > intervals[i]" terms is exactly j -- no further
// adjustment needed.
template <BooleanContext Ctx, int N, int M>
CubeIndex<Ctx, M> select_cube_index(const DnfWeight<Ctx, N, M>& z,
                                     const array<DnfWeight<Ctx, N, M>, (size_t)M + 1>& intervals) {
    using Idx = CubeIndex<Ctx, M>;
    Ctx& ctx = *z.context();

    Idx zero = Idx::constant(ctx, 0);
    Idx one  = Idx::constant(ctx, 1);
    Idx count = zero;
    for (size_t i = 0; i < (size_t)M + 1; ++i) {
        Bit_T<Ctx> gt = z > intervals[i];
        count = count + zero.select(gt, one);  // gt ? 1 : 0
    }
    return count;
}

// Just the payload select_cube() below hands back: no `pad`, since a
// validly-sampled index always lands in a non-padding cube (a padding
// cube has weight 0 -- gadgets/cube_weight.h -- so it occupies a
// zero-width slice of the intervals and z can never land in it).
template <BooleanContext Ctx, int N>
struct CubeData {
    BitVec_T<Ctx, N> bits;
    BitVec_T<Ctx, N> mask;
};

// Oblivious lookup of cubes[index-1]'s bits/mask (index is 1-indexed, as
// returned by select_cube_index): a linear-scan mux over all M candidates
// -- O(M) equality comparisons and selects, fine for the small M this
// pipeline uses. cubes[0] is the fold's starting default, overwritten by
// cubes[i] wherever index == i+1.
template <BooleanContext Ctx, int N, int M>
CubeData<Ctx, N> cube_at_index(const CubeIndex<Ctx, M>& index,
                                const array<CircuitCube<Ctx, N>, (size_t)M>& cubes) {
    Ctx& ctx = *index.context();
    BitVec_T<Ctx, N> bits = cubes[0].bits;
    BitVec_T<Ctx, N> mask = cubes[0].mask;
    for (size_t i = 1; i < (size_t)M; ++i) {
        Bit_T<Ctx> match = index == CubeIndex<Ctx, M>::constant(ctx, (uint64_t)(i + 1));
        bits = bits.select(match, cubes[i].bits);
        mask = mask.select(match, cubes[i].mask);
    }
    return CubeData<Ctx, N>{bits, mask};
}

// Composes the three pieces above: sample, then find which cube that
// sample lands in, then look up that cube's bits/mask. The intermediate
// sample and index aren't returned -- callers that want to inspect those
// too (e.g. for testing) can call sample_in_range()/select_cube_index()
// themselves instead of this composed function.
template <BooleanContext Ctx, int N, int M>
CubeData<Ctx, N> select_cube(const SampleBits<Ctx, N, M>& alice_r,
                              const SampleBits<Ctx, N, M>& bob_r,
                              const DnfWeight<Ctx, N, M>& total,
                              const array<DnfWeight<Ctx, N, M>, (size_t)M + 1>& intervals,
                              const array<CircuitCube<Ctx, N>, (size_t)M>& cubes) {
    DnfWeight<Ctx, N, M> z = sample_in_range<Ctx, N, M>(alice_r, bob_r, total);
    CubeIndex<Ctx, M> j = select_cube_index<Ctx, N, M>(z, intervals);
    return cube_at_index<Ctx, N, M>(j, cubes);
}

}  // namespace gadgets
