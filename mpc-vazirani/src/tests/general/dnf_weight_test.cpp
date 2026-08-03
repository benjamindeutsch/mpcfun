// Unit tests for gadgets/dnf/dnf_weight.h, run entirely in the clear via
// emp::ClearSession: no OT, no network, no garbling, single process --
// same approach as cube_weight_test.cpp.
//
// run_dnf_weight_tests() is called from tests/run_tests.cpp's main(), the
// single entry point for every *_test.cpp under tests/ -- see that file.

#include "gadgets/dnf/dnf_weight.h"
#include "gadgets/dnf/cube_weight.h"
#include "tests/test_helpers.h"
#include "emp-tool/ir/session/clear_session.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {

using namespace emp;
using namespace gadgets;

constexpr int N = 4;
using Ctx = ClearSession::ctx_t;

template <int M>
void check(ClearSession& sess, const char* name, const std::array<CircuitCube<Ctx, N>, M>& cubes, uint64_t expect) {
    std::array<CubeWeight<Ctx, N>, M> weights = cube_weights<Ctx, N, M>(cubes);
    uint64_t got = sess.reveal(dnf_weight<Ctx, N, M>(weights), PUBLIC).value();
    if (got != expect) {
        std::fprintf(stderr, "FAIL %s (got %llu, expected %llu)\n", name,
                      (unsigned long long)got, (unsigned long long)expect);
        std::exit(1);
    }
    std::printf("PASS %s\n", name);
}

}  // namespace

int run_dnf_weight_tests() {
    ClearSession sess;

    // A single cube: the sum is just its own weight.
    check<1>(sess, "single cube: sum = its own weight",
              {make_cube<ClearSession, Ctx, N>(sess, "0000", "0000")}, 16);

    // Empty (16) + fully-constrained (1) + single-literal (8) + padding (0)
    // -- the exact cubes from cube_weight_test.cpp -- sums to 25. Padding
    // contributes 0, so it doesn't skew the total.
    check<4>(sess, "empty+full+single+padding: sum = 16+1+8+0 = 25",
              {make_cube<ClearSession, Ctx, N>(sess, "0000", "0000"),                // r=0 -> 16
               make_cube<ClearSession, Ctx, N>(sess, "1010", "1111"),                // r=4 -> 1
               make_cube<ClearSession, Ctx, N>(sess, "0100", "0100"),                // r=1 -> 8
               make_cube<ClearSession, Ctx, N>(sess, "1111", "0000", /*pad=*/true)},  // padding -> 0
              25);

    // Four ordinary cubes with no padding: 4+8+1+1 = 14.
    check<4>(sess, "four ordinary cubes: sum = 4+8+1+1 = 14",
              {make_cube<ClearSession, Ctx, N>(sess, "1000", "1010"),   // r=2 -> 4
               make_cube<ClearSession, Ctx, N>(sess, "0100", "0100"),   // r=1 -> 8
               make_cube<ClearSession, Ctx, N>(sess, "1111", "1111"),   // r=4 -> 1
               make_cube<ClearSession, Ctx, N>(sess, "0000", "1111")},  // r=4 -> 1
              14);

    std::printf("dnf_weight_test: all checks passed\n");
    return 0;
}
