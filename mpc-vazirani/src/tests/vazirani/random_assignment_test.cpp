// Unit tests for gadgets/vazirani/random_assignment.h, run entirely in the
// clear via emp::ClearSession: no OT, no network, no garbling, single
// process -- same approach as select_cube_test.cpp.
//
// run_random_assignment_tests() is called from tests/run_tests.cpp's
// main(), the single entry point for every *_test.cpp under tests/ -- see
// that file.

#include "gadgets/vazirani/random_assignment.h"
#include "tests/bits_of.h"
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

void check(ClearSession& sess, const char* name, const char* cube_bits, const char* cube_mask,
           const char* alice_r, const char* bob_r, const char* expect) {
    CubeData<Ctx, N> cube{
        sess.input<BV>(PUBLIC, bits_of<N>(cube_bits)),
        sess.input<BV>(PUBLIC, bits_of<N>(cube_mask)),
    };
    BV a = sess.input<BV>(PUBLIC, bits_of<N>(alice_r));
    BV b = sess.input<BV>(PUBLIC, bits_of<N>(bob_r));

    std::array<bool, N> got = sess.reveal(random_assignment<Ctx, N>(cube, a, b), PUBLIC).value();
    if (got != bits_of<N>(expect)) {
        std::fprintf(stderr, "FAIL %s\n", name);
        std::exit(1);
    }
    std::printf("PASS %s\n", name);
}

}  // namespace

int run_random_assignment_tests() {
    ClearSession sess;

    // Fully-constrained cube (mask=1111): the assignment is exactly the
    // cube's own bits, regardless of r -- nothing is free to fill in.
    check(sess, "fully-constrained: assignment = cube bits, r=0",
          "1010", "1111", "0000", "0000", "1010");
    check(sess, "fully-constrained: assignment = cube bits, r nonzero",
          "1010", "1111", "1111", "0101", "1010");

    // Empty cube (mask=0000): nothing is fixed, so the assignment is
    // exactly r = alice_r ^ bob_r = 1100^1010 = 0110.
    check(sess, "empty cube: assignment = alice_r ^ bob_r",
          "0000", "0000", "1100", "1010", "0110");

    // Partially-constrained (mask=1000, only var0 fixed to 1): var0 keeps
    // the cube's bit; vars 1-3 take r's bits. r = 1101^0011 = 1110, so
    // assignment = 1000 | (~1000 & 1110) = 1000 | 0110 = 1110.
    check(sess, "partial: fixed bit kept, free bits filled from r",
          "1000", "1000", "1101", "0011", "1110");

    // Same cube, different r -> different free bits, same fixed bit.
    // r = 0000^0000 = 0000, so assignment = 1000 | (0111&0000) = 1000.
    check(sess, "partial: different r gives different free bits",
          "1000", "1000", "0000", "0000", "1000");

    std::printf("random_assignment_test: all checks passed\n");
    return 0;
}
