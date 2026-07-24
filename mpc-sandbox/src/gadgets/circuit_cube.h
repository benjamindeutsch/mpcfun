// CircuitCube: the wire-level counterpart of dimacs_dnf::Cube (bits/mask/pad
// have the same meaning, just as emp::BitVec_T<Ctx,N>/emp::Bit_T<Ctx> wires
// instead of vector<bool>/bool). A shared foundational type -- not specific
// to any one gadget -- that any circuit working with DNF cubes builds on
// (see gadgets/dnf_distribute.h, gadgets/cube_weight.h).
//
// Ctx-generic (any emp::BooleanContext), not tied to a specific session:
// the same type works under emp::ClearSession (plaintext, for fast gadget
// unit tests) and emp::SH2PCSession (the real 2PC protocol) unchanged.

#pragma once

#include "emp-tool/circuits/typed.h"
#include "emp-tool/circuits/numeric_kernels.h"

using emp::BitVec_T;
using emp::Bit_T;
using emp::UInt_T;
using emp::BooleanContext;
using emp::kernel::bits_for;

namespace gadgets {

template <BooleanContext Ctx, int N>
struct CircuitCube {
    BitVec_T<Ctx, N> bits;
    BitVec_T<Ctx, N> mask;
    Bit_T<Ctx> pad;
};

// Wide enough to hold the largest possible weight, 2^N (an empty/all-free
// cube, r=0), without overflow: 2^N in binary is a 1 followed by N zeros --
// N+1 bits exactly.
template <BooleanContext Ctx, int N>
using CubeWeight = UInt_T<Ctx, N + 1>;

// Wide enough to hold the largest possible sum of M CubeWeight<Ctx,N> terms
// (each up to 2^N): M * 2^N < 2^(N + bits_for(M)), so N + bits_for(M) bits
// always suffice, regardless of how large N itself is (bits_for(M) depends
// only on the compile-time cube count M, not on 2^N).
template <BooleanContext Ctx, int N, int M>
using DnfWeight = UInt_T<Ctx, N + bits_for(M)>;

}  // namespace gadgets
