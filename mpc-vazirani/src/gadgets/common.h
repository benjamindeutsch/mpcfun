// Shared includes/using-declarations for every header under gadgets/, plus
// two small cross-cutting helpers that several gadgets each independently
// reimplemented. Every gadgets/ header includes this instead of repeating
// the same boilerplate.

#pragma once

#include "emp-tool/circuits/typed.h"
#include "emp-tool/circuits/numeric_kernels.h"

#include <array>
#include <cstddef>

using std::array;
using std::size_t;
using emp::BitVec_T;
using emp::Bit_T;
using emp::UInt_T;
using emp::BooleanContext;
using emp::kernel::bits_for;

namespace gadgets {

// v.zext<W::width()>() doesn't parse (GCC rejects a member-function call
// directly inside a template-argument list in a dependent context), so
// every gadget that needed to widen a value to another WireValue type's
// width used to hoist `constexpr int Width = W::width();` locally first.
// zext_to<W>(v) does that hoisting once, here.
template <class W, class V>
W zext_to(const V& v) {
    constexpr int Width = W::width();
    return v.template zext<Width>();
}

// cond ? 1 : 0, as a Count -- the fold step every "count how many of these
// conditions hold" gadget (select_cube_index, count_satisfied_cubes)
// accumulates over its candidates.
template <class Count, BooleanContext Ctx>
Count indicator(Ctx& ctx, const Bit_T<Ctx>& cond) {
    return Count::constant(ctx, 0).select(cond, Count::constant(ctx, 1));
}

}  // namespace gadgets
