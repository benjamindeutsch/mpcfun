// Unit tests for gadgets/dnf/cube_weight.h, run entirely in the clear via
// emp::ClearSession: no OT, no network, no garbling, single process --
// same approach as dnf_distribute_test.cpp.
//
// run_cube_weight_tests() is called from tests/run_tests.cpp's main(), the
// single entry point for every *_test.cpp under tests/ -- see that file.

#include "gadgets/dnf/cube_weight.h"
#include "tests/test_helpers.h"
#include "emp-tool/ir/session/clear_session.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace {

using namespace emp;
using namespace gadgets;

constexpr int N = 4;
using Ctx = ClearSession::ctx_t;

void check(ClearSession& sess, const char* name, const CircuitCube<Ctx, N>& c, uint64_t expect) {
    uint64_t got = sess.reveal(cube_weight<Ctx, N>(c), PUBLIC).value();
    if (got != expect) {
        std::fprintf(stderr, "FAIL %s (got %llu, expected %llu)\n", name,
                      (unsigned long long)got, (unsigned long long)expect);
        std::exit(1);
    }
    std::printf("PASS %s\n", name);
}

}  // namespace

int run_cube_weight_tests() {
    ClearSession sess;

    // Empty cube (no constraints, r=0) -> every one of the 2^4 assignments
    // over 4 variables satisfies it.
    check(sess, "empty cube: weight = 2^VARS",
          make_cube<ClearSession, Ctx, N>(sess, "0000", "0000"), 16);

    // Fully-constrained cube (r=VARS) -> exactly one assignment satisfies it.
    check(sess, "fully-constrained cube: weight = 1",
          make_cube<ClearSession, Ctx, N>(sess, "1010", "1111"), 1);

    // r=2 constrained, 2 free -> 2^2 = 4.
    check(sess, "half-constrained cube: weight = 2^(VARS-r)",
          make_cube<ClearSession, Ctx, N>(sess, "1000", "1010"), 4);

    // r=1 constrained, 3 free -> 2^3 = 8.
    check(sess, "single-literal cube: weight = 2^(VARS-1)",
          make_cube<ClearSession, Ctx, N>(sess, "0100", "0100"), 8);

    // A padding cube always has weight 0, regardless of its bits/mask --
    // even though mask=0^N would otherwise give it weight 2^VARS, same as a
    // genuine empty cube (it isn't a real disjunct, so it must not
    // contribute to a sum of weights across a DNF).
    check(sess, "padding cube: weight = 0",
          make_cube<ClearSession, Ctx, N>(sess, "1111", "0000", /*pad=*/true), 0);

    // cube_weights: the array form matches calling cube_weight() one by one.
    std::array<CircuitCube<Ctx, N>, 4> cubes = {
        make_cube<ClearSession, Ctx, N>(sess, "0000", "0000"),                 // r=0 -> 16
        make_cube<ClearSession, Ctx, N>(sess, "1111", "1111"),                 // r=4 -> 1
        make_cube<ClearSession, Ctx, N>(sess, "0100", "0100"),                 // r=1 -> 8
        make_cube<ClearSession, Ctx, N>(sess, "1111", "0000", /*pad=*/true),   // padding -> 0
    };
    std::array<CubeWeight<Ctx, N>, 4> weights = cube_weights<Ctx, N, 4>(cubes);
    assert(sess.reveal(weights[0], PUBLIC).value() == 16);
    assert(sess.reveal(weights[1], PUBLIC).value() == 1);
    assert(sess.reveal(weights[2], PUBLIC).value() == 8);
    assert(sess.reveal(weights[3], PUBLIC).value() == 0);
    std::printf("PASS cube_weights matches cube_weight one-by-one\n");

    std::printf("cube_weight_test: all checks passed\n");
    return 0;
}
