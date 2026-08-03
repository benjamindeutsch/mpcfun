// Unit tests for gadgets/vazirani/count_satisfied_cubes.h, run entirely in
// the clear via emp::ClearSession: no OT, no network, no garbling, single
// process -- same approach as select_cube_test.cpp.
//
// run_count_satisfied_cubes_tests() is called from tests/run_tests.cpp's
// main(), the single entry point for every *_test.cpp under tests/ -- see
// that file.

#include "gadgets/vazirani/count_satisfied_cubes.h"
#include "tests/test_helpers.h"
#include "emp-tool/ir/session/clear_session.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {

using namespace emp;
using namespace gadgets;

constexpr int N = 4;
constexpr int M = 4;
using Ctx = ClearSession::ctx_t;
using BV  = BitVec_T<Ctx, N>;

}  // namespace

int run_count_satisfied_cubes_tests() {
    ClearSession sess;

    // cube1: x1. cube2: x2. cube3: x3. cube4: a padding cube
    // (bits=1111, mask=0000), which must never count as satisfied.
    std::array<CircuitCube<Ctx, N>, M> cubes = {
        make_cube<ClearSession, Ctx, N>(sess, "1000", "1000"),
        make_cube<ClearSession, Ctx, N>(sess, "0100", "0100"),
        make_cube<ClearSession, Ctx, N>(sess, "0010", "0010"),
        make_cube<ClearSession, Ctx, N>(sess, "1111", "0000", /*pad=*/true),
    };

    auto check = [&](const char* name, const char* assignment_str, uint64_t expect) {
        BV assignment = sess.input<BV>(PUBLIC, bits_of<N>(assignment_str));
        uint64_t got = sess.reveal(count_satisfied_cubes<Ctx, N, M>(assignment, cubes), PUBLIC).value();
        if (got != expect) {
            std::fprintf(stderr, "FAIL %s (got %llu, expected %llu)\n", name,
                          (unsigned long long)got, (unsigned long long)expect);
            std::exit(1);
        }
        std::printf("PASS %s\n", name);
    };

    // Satisfies none of cube1-3 (all need some bit set), and never the
    // padding cube regardless.
    check("assignment=0000 satisfies 0 cubes", "0000", 0);

    // x1=1 only -> satisfies cube1 alone.
    check("assignment=1000 satisfies 1 cube (cube1)", "1000", 1);

    // x1=1, x2=1 -> satisfies cube1 and cube2.
    check("assignment=1100 satisfies 2 cubes (cube1,cube2)", "1100", 2);

    // x1=1, x2=1, x3=1 -> satisfies cube1, cube2, cube3 -- but never the
    // padding cube, even though every other bit is set too.
    check("assignment=1110 satisfies 3 cubes (cube1,cube2,cube3), never padding", "1110", 3);

    // All bits set, including the padding cube's own bits pattern (1111)
    // -- still doesn't satisfy the padding cube, since its mask is 0000.
    check("assignment=1111 still excludes the padding cube", "1111", 3);

    std::printf("count_satisfied_cubes_test: all checks passed\n");
    return 0;
}
