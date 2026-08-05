// is_canonical_sample: the Karp-Luby-specific step-4 divergence point from
// Vazirani (see gadgets/vazirani/count_satisfied_cubes.h, the analogous
// Vazirani piece). Where Vazirani counts *how many* cubes a sampled
// assignment satisfies and looks up 1/count, Karp-Luby only needs a single
// bit: is the sampled cube the *canonical* (lowest-indexed) cube covering
// this assignment, i.e. does no earlier-indexed cube also satisfy it?
//
// `index` is 1-indexed, matching gadgets/general/select_cube.h's
// select_cube_index output; cubes[j] (0-indexed) corresponds to index
// value j+1. The per-cube satisfied check ((assignment & mask) == bits) is
// the same one gadgets/vazirani/count_satisfied_cubes.h uses; here it's
// combined with an "earlier than the sampled cube" comparison and OR'd
// across all cubes instead of summed.
//
// Ctx-generic (any BooleanContext), not tied to a specific session, like
// every other gadget in this project: the same code type-checks under
// ClearSession (for fast unit tests) and SH2PCSession (the real 2PC
// protocol) unchanged.

#pragma once

#include "gadgets/circuit_cube.h"
#include "gadgets/common.h"
#include "gadgets/general/select_cube.h"

namespace gadgets {

// Single left-to-right scan over the M cubes (1-indexed j = 1..M, matching
// `index`), maintaining two running bits:
//
//   b2 = OR of "cube j' satisfied the assignment" over every j' scanned so
//        far (i.e., strictly before the current j, since it's updated
//        *after* being read at j == index).
//   b1 = frozen to !b2 at the one iteration where j == index -- i.e. "no
//        cube with index < the sampled index also satisfies this
//        assignment" -- and left untouched every other iteration, so it
//        carries that frozen value through to the end of the scan.
//
// b1's final value is exactly the canonical-sample predicate: true iff no
// earlier-indexed cube also satisfies the assignment. Equivalent to (and
// cheaper than) an explicit OR-reduction of (satisfied_j & (j < index))
// over all j, since it needs one equality (j == index) per cube instead of
// one less-than.
//
// Padding cubes are never satisfied ((assignment & 0^N) == 1^N is always
// false -- see gadgets/vazirani/count_satisfied_cubes.h's comment), so they
// don't need special-casing here either.
template <BooleanContext Ctx, int N, int M>
Bit_T<Ctx> is_canonical_sample(const CubeIndex<Ctx, M>& index,
                                const BitVec_T<Ctx, N>& assignment,
                                const array<CircuitCube<Ctx, N>, (size_t)M>& cubes) {
    Ctx& ctx = *assignment.context();
    using Idx = CubeIndex<Ctx, M>;

    Bit_T<Ctx> b1 = Bit_T<Ctx>::constant(ctx, true);
    Bit_T<Ctx> b2 = Bit_T<Ctx>::constant(ctx, false);

    for (size_t j = 0; j < (size_t)M; ++j) {
        Bit_T<Ctx> satisfied = (assignment & cubes[j].mask) == cubes[j].bits;
        Bit_T<Ctx> is_current = index == Idx::constant(ctx, (uint64_t)(j + 1));
        b1 = b1.select(is_current, !b2);
        b2 = b2 | satisfied;
    }
    return b1;
}

}  // namespace gadgets
