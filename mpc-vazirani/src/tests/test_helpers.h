// Shared emp-tool-dependent test helpers, built on top of tests/bits_of.h:
// bits_of_uint<WIDTH> (a uint64_t's bits, for gadgets/vazirani/select_cube.h's
// SampleBits-shaped inputs) and make_cube<Ctx,N> (build a CircuitCube from
// two bit strings + a PUBLIC input, on whatever session type -- ClearSession
// throughout this test suite -- the caller passes in). Every gadget test
// under tests/dnf/ and tests/vazirani/ used to define its own near-copy of
// these; this is the one shared version.

#pragma once

#include "tests/bits_of.h"
#include "gadgets/circuit_cube.h"
#include "emp-tool/circuits/typed.h"

#include <array>
#include <cstddef>
#include <cstdint>

template <int WIDTH>
std::array<bool, WIDTH> bits_of_uint(uint64_t v) {
    std::array<bool, WIDTH> b{};
    for (int i = 0; i < WIDTH; ++i) b[(std::size_t)i] = ((v >> i) & 1) != 0;
    return b;
}

template <class Session, emp::BooleanContext Ctx, int N>
gadgets::CircuitCube<Ctx, N> make_cube(Session& sess, const char* bits, const char* mask, bool pad = false) {
    return gadgets::CircuitCube<Ctx, N>{
        sess.template input<emp::BitVec_T<Ctx, N>>(emp::PUBLIC, bits_of<N>(bits)),
        sess.template input<emp::BitVec_T<Ctx, N>>(emp::PUBLIC, bits_of<N>(mask)),
        sess.template input<emp::Bit_T<Ctx>>(emp::PUBLIC, pad),
    };
}
