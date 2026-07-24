// conjoin/conjoin_dnf: distributing conjunction over a DNF's disjunction of
// CircuitCube (see gadgets/circuit_cube.h for that type).
//
// Templated over Ctx (any BooleanContext) and N (variable count), not
// tied to a specific session. That's what makes this a separate testable
// unit: the exact same code type-checks against ClearSession
// (plaintext, no OT/network -- see tests/dnf_distribute_test.cpp) and
// against SH2PCSession (the real 2PC protocol), with zero changes.
// Write and debug the gate logic under ClearSession, then reuse it as-is
// once it's wired into the bigger circuit.

#pragma once

#include "gadgets/circuit_cube.h"
#include "emp-tool/circuits/typed.h"

#include <array>
#include <cstddef>

using std::array;
using std::size_t;
using emp::BitVec_T;
using emp::Bit_T;
using emp::BooleanContext;

namespace gadgets {

// a /\ b. Padding is contagious: the result is padding (pad=true, bits
// pinned to 1^N, mask pinned to 0^N) if EITHER input is already padding, or
// if a and b disagree on some variable they both constrain (making their
// conjunction unsatisfiable -- there's no "no such cube" value, so that has
// to be flagged the same way as padding). Otherwise it's the ordinary
// per-bit-mux conjunction:
//   mask = a.mask | b.mask                        (constrained by either)
//   bits = (a.bits & a.mask) | (b.bits & ~a.mask)  (per-bit mux: take a's
//          value where a constrains that variable, else b's)
template <BooleanContext Ctx, int N>
CircuitCube<Ctx, N> conjoin(const CircuitCube<Ctx, N>& a, const CircuitCube<Ctx, N>& b) {
    BitVec_T<Ctx, N> zero =
        BitVec_T<Ctx, N>::constant(*a.bits.context(), array<bool, (size_t)N>{});
    BitVec_T<Ctx, N> disagreement = (a.bits ^ b.bits) & a.mask & b.mask;
    Bit_T<Ctx> disagree = !(disagreement == zero);
    Bit_T<Ctx> pad = a.pad | b.pad | disagree;

    BitVec_T<Ctx, N> bits = (a.bits & a.mask) | (b.bits & ~a.mask);
    BitVec_T<Ctx, N> mask = a.mask | b.mask;

    return CircuitCube<Ctx, N>{
        bits.select(pad, ~zero),  // pad ? 1^N : bits
        mask.select(pad, zero),   // pad ? 0^N : mask
        pad,
    };
}

// The cubes of d1 /\ d2: disjunction distributes over conjunction, so
// (OR_i a_i) /\ (OR_j b_j) = OR_{i,j} (a_i /\ b_j) -- the pairwise
// conjunction of every cube in d1 with every cube in d2, CUBES of each ->
// CUBES*CUBES results. Contradictory pairings come back as padding cubes
// (see conjoin()), so downstream consumers (e.g. cube_weight, which gives
// padding cubes weight 0) already treat them as absent without needing to
// filter them out here.
template <BooleanContext Ctx, int N, int CUBES>
array<CircuitCube<Ctx, N>, (size_t)CUBES * (size_t)CUBES> conjoin_dnf(
    const array<CircuitCube<Ctx, N>, (size_t)CUBES>& d1,
    const array<CircuitCube<Ctx, N>, (size_t)CUBES>& d2) {
    array<CircuitCube<Ctx, N>, (size_t)CUBES * (size_t)CUBES> out{};
    size_t idx = 0;
    for (const auto& a : d1)
        for (const auto& b : d2)
            out[idx++] = conjoin<Ctx, N>(a, b);
    return out;
}

}  // namespace gadgets
