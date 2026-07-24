// Unit tests for gadgets/sample_cube.h, run entirely in the clear via
// emp::ClearSession: no OT, no network, no garbling, single process --
// same approach as dnf_weight_test.cpp.
//
// ClearSession can't demonstrate the randomness/security property of the
// free-XOR coin flip (everything is a public wire there) -- what it CAN
// check is that the wire-level arithmetic (XOR, mod, +1) is correct, by
// feeding in known alice_r/bob_r/total values and checking the result
// against hand-computed expectations.
//
// run_sample_cube_tests() is called from tests/run_tests.cpp's main(), the
// single entry point for every *_test.cpp under tests/ -- see that file.

#include "gadgets/sample_cube.h"
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
using W   = DnfWeight<Ctx, N, M>;
using RB  = SampleBits<Ctx, N, M>;
constexpr int WIDTH = N + bits_for(M);  // = 7

std::array<bool, WIDTH> bits_of_uint(uint64_t v) {
    std::array<bool, WIDTH> b{};
    for (int i = 0; i < WIDTH; ++i) b[(std::size_t)i] = ((v >> i) & 1) != 0;
    return b;
}

void check(ClearSession& sess, const char* name, uint64_t alice_r, uint64_t bob_r, uint64_t total,
           uint64_t expect) {
    RB a = sess.input<RB>(PUBLIC, bits_of_uint(alice_r));
    RB b = sess.input<RB>(PUBLIC, bits_of_uint(bob_r));
    W  t = sess.input<W>(PUBLIC, total);

    uint64_t got = sess.reveal(sample_cube<Ctx, N, M>(a, b, t), PUBLIC).value();
    if (got != expect) {
        std::fprintf(stderr, "FAIL %s (got %llu, expected %llu)\n", name,
                      (unsigned long long)got, (unsigned long long)expect);
        std::exit(1);
    }
    std::printf("PASS %s\n", name);
}

}  // namespace

int run_sample_cube_tests() {
    ClearSession sess;

    // Straightforward: 13 ^ 7 = 10, 10 % 20 = 10, +1 = 11.
    check(sess, "basic: (13^7)%20+1 = 11", 13, 7, 20, 11);

    // One contribution is 0 -- the joint value is just the other party's,
    // still demonstrating the mod+shift: 25 % 20 = 5, +1 = 6.
    check(sess, "wraps above total: 25%20+1 = 6", 25, 0, 20, 6);

    // joint_r an exact multiple of total -> minimum possible sample, 1.
    check(sess, "exact multiple of total -> sample = 1", 20, 0, 20, 1);

    // joint_r = total-1 -> maximum possible sample, equal to total itself.
    check(sess, "joint_r = total-1 -> sample = total", 19, 0, 20, 20);

    // Both contributions nonzero: 100^93 = 57, 57%20 = 17, +1 = 18.
    check(sess, "both contributions nonzero: (100^93)%20+1 = 18", 100, 93, 20, 18);

    std::printf("sample_cube_test: all checks passed\n");
    return 0;
}
