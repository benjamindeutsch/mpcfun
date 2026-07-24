// Unit tests for gadgets/cube_intervals.h, run entirely in the clear via
// emp::ClearSession: no OT, no network, no garbling, single process --
// same approach as dnf_weight_test.cpp.
//
// run_cube_intervals_tests() is called from tests/run_tests.cpp's main(),
// the single entry point for every *_test.cpp under tests/ -- see that file.

#include "gadgets/cube_intervals.h"
#include "gadgets/cube_weight.h"
#include "emp-tool/ir/session/clear_session.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {

using namespace emp;
using namespace gadgets;

constexpr int N = 4;
using Ctx = ClearSession::ctx_t;
using BV  = BitVec_T<Ctx, N>;

std::array<bool, N> bits_of(const char* s) {
    std::array<bool, N> b{};
    for (int i = 0; i < N; ++i) b[(std::size_t)i] = (s[i] == '1');
    return b;
}

CircuitCube<Ctx, N> make_cube(ClearSession& sess, const char* bits, const char* mask, bool pad = false) {
    return CircuitCube<Ctx, N>{
        sess.input<BV>(PUBLIC, bits_of(bits)),
        sess.input<BV>(PUBLIC, bits_of(mask)),
        sess.input<Bit_T<Ctx>>(PUBLIC, pad),
    };
}

template <int M>
void check(ClearSession& sess, const char* name,
           const std::array<CircuitCube<Ctx, N>, M>& cubes,
           const std::array<uint64_t, M + 1>& expect) {
    std::array<CubeWeight<Ctx, N>, M> weights = cube_weights<Ctx, N, M>(cubes);
    std::array<DnfWeight<Ctx, N, M>, M + 1> intervals = cube_intervals<Ctx, N, M>(weights);

    for (int i = 0; i < M + 1; ++i) {
        uint64_t got = sess.reveal(intervals[(std::size_t)i], PUBLIC).value();
        if (got != expect[(std::size_t)i]) {
            std::fprintf(stderr, "FAIL %s (intervals[%d]: got %llu, expected %llu)\n", name, i,
                          (unsigned long long)got, (unsigned long long)expect[(std::size_t)i]);
            std::exit(1);
        }
    }
    std::printf("PASS %s\n", name);
}

}  // namespace

int run_cube_intervals_tests() {
    ClearSession sess;

    // Weights [16, 1, 8, 0] (the exact cubes from cube_weight_test.cpp) ->
    // T_0=0, T_1=0+16=16, T_2=16+1=17, T_3=17+8=25, T_4=25+0=25. T_4, the
    // last boundary, equals the total weight (matches
    // dnf_weight_test.cpp's "empty+full+single+padding" case: 25).
    check<4>(sess, "empty+full+single+padding: intervals = [0,16,17,25,25]",
              {make_cube(sess, "0000", "0000"),                 // r=0 -> 16
               make_cube(sess, "1010", "1111"),                 // r=4 -> 1
               make_cube(sess, "0100", "0100"),                 // r=1 -> 8
               make_cube(sess, "1111", "0000", /*pad=*/true)},  // padding -> 0
              {0, 16, 17, 25, 25});

    // Weights [4, 8, 1, 1] (the exact cubes from dnf_weight_test.cpp's
    // "four ordinary cubes" case) -> T_0=0, T_1=4, T_2=12, T_3=13, T_4=14
    // (matches that test's total: 14).
    check<4>(sess, "four ordinary cubes: intervals = [0,4,12,13,14]",
              {make_cube(sess, "1000", "1010"),   // r=2 -> 4
               make_cube(sess, "0100", "0100"),   // r=1 -> 8
               make_cube(sess, "1111", "1111"),   // r=4 -> 1
               make_cube(sess, "0000", "1111")},  // r=4 -> 1
              {0, 4, 12, 13, 14});

    // A single cube: intervals = [0, its own weight].
    check<1>(sess, "single cube: intervals = [0,16]",
              {make_cube(sess, "0000", "0000")},
              {0, 16});

    std::printf("cube_intervals_test: all checks passed\n");
    return 0;
}
