// Unit tests for gadgets/vazirani/select_cube.h, run entirely in the clear
// via emp::ClearSession: no OT, no network, no garbling, single process --
// same approach as dnf_weight_test.cpp.
//
// ClearSession can't demonstrate the randomness/security property of the
// free-XOR coin flip (everything is a public wire there) -- what it CAN
// check is that the wire-level arithmetic is correct, by feeding in known
// values and checking the result against hand-computed expectations.
//
// run_select_cube_tests() is called from tests/run_tests.cpp's main(), the
// single entry point for every *_test.cpp under tests/ -- see that file.

#include "gadgets/vazirani/select_cube.h"
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
using W   = DnfWeight<Ctx, N, M>;
using RB  = SampleBits<Ctx, N, M>;
using Idx = CubeIndex<Ctx, M>;
constexpr int WIDTH = N + bits_for(M);  // = 7

void fail(const char* name) {
    std::fprintf(stderr, "FAIL %s\n", name);
    std::exit(1);
}

void check_sample(ClearSession& sess, const char* name, uint64_t alice_r, uint64_t bob_r, uint64_t total,
                   uint64_t expect) {
    RB a = sess.input<RB>(PUBLIC, bits_of_uint<WIDTH>(alice_r));
    RB b = sess.input<RB>(PUBLIC, bits_of_uint<WIDTH>(bob_r));
    W  t = sess.input<W>(PUBLIC, total);

    uint64_t got = sess.reveal(sample_in_range<Ctx, N, M>(a, b, t), PUBLIC).value();
    if (got != expect) {
        std::fprintf(stderr, "FAIL %s (got %llu, expected %llu)\n", name,
                      (unsigned long long)got, (unsigned long long)expect);
        std::exit(1);
    }
    std::printf("PASS %s\n", name);
}

void check_index(ClearSession& sess, const char* name, uint64_t z,
                  const std::array<uint64_t, M + 1>& intervals, uint64_t expect) {
    W zw = sess.input<W>(PUBLIC, z);
    std::array<W, M + 1> iw{};
    for (int i = 0; i < M + 1; ++i) iw[(std::size_t)i] = sess.input<W>(PUBLIC, intervals[(std::size_t)i]);

    uint64_t got = sess.reveal(select_cube_index<Ctx, N, M>(zw, iw), PUBLIC).value();
    if (got != expect) {
        std::fprintf(stderr, "FAIL %s (got %llu, expected %llu)\n", name,
                      (unsigned long long)got, (unsigned long long)expect);
        std::exit(1);
    }
    std::printf("PASS %s\n", name);
}

}  // namespace

int run_select_cube_tests() {
    ClearSession sess;

    // sample_in_range -------------------------------------------------

    // Straightforward: 13 ^ 7 = 10, 10 % 20 = 10, +1 = 11.
    check_sample(sess, "sample: (13^7)%20+1 = 11", 13, 7, 20, 11);

    // One contribution is 0 -- the joint value is just the other party's,
    // still demonstrating the mod+shift: 25 % 20 = 5, +1 = 6.
    check_sample(sess, "sample: wraps above total: 25%20+1 = 6", 25, 0, 20, 6);

    // joint_r an exact multiple of total -> minimum possible sample, 1.
    check_sample(sess, "sample: exact multiple of total -> sample = 1", 20, 0, 20, 1);

    // joint_r = total-1 -> maximum possible sample, equal to total itself.
    check_sample(sess, "sample: joint_r = total-1 -> sample = total", 19, 0, 20, 20);

    // Both contributions nonzero: 100^93 = 57, 57%20 = 17, +1 = 18.
    check_sample(sess, "sample: both contributions nonzero: (100^93)%20+1 = 18", 100, 93, 20, 18);

    // select_cube_index -------------------------------------------------

    // The exact boundaries from the alice/bob demo pipeline,
    // intervals=[0,8,12,16,20] (M=4). j is 1-indexed: the unique j with
    // intervals[j-1] < z <= intervals[j].
    std::array<uint64_t, M + 1> intervals = {0, 8, 12, 16, 20};
    check_index(sess, "index: z=5 -> j=1 (0<5<=8)", 5, intervals, 1);
    check_index(sess, "index: z=8 -> j=1 (boundary: 0<8<=8)", 8, intervals, 1);
    check_index(sess, "index: z=9 -> j=2 (8<9<=12)", 9, intervals, 2);
    check_index(sess, "index: z=12 -> j=2 (boundary: 8<12<=12)", 12, intervals, 2);
    check_index(sess, "index: z=13 -> j=3 (12<13<=16)", 13, intervals, 3);
    check_index(sess, "index: z=17 -> j=4 (16<17<=20)", 17, intervals, 4);
    check_index(sess, "index: z=20 -> j=4 (boundary: 16<20<=20)", 20, intervals, 4);
    check_index(sess, "index: z=1 -> j=1 (minimum sample)", 1, intervals, 1);

    // cube_at_index -------------------------------------------------

    std::array<CircuitCube<Ctx, N>, M> cubes = {
        make_cube<ClearSession, Ctx, N>(sess, "1000", "1000"),  // cube 1 (1-indexed)
        make_cube<ClearSession, Ctx, N>(sess, "0100", "0100"),  // cube 2
        make_cube<ClearSession, Ctx, N>(sess, "0010", "0010"),  // cube 3
        make_cube<ClearSession, Ctx, N>(sess, "0001", "0001"),  // cube 4
    };
    for (uint64_t j = 1; j <= (uint64_t)M; ++j) {
        Idx idx = sess.input<Idx>(PUBLIC, j);
        CubeData<Ctx, N> got = cube_at_index<Ctx, N, M>(idx, cubes);
        std::array<bool, N> bits = sess.reveal(got.bits, PUBLIC).value();
        std::array<bool, N> mask = sess.reveal(got.mask, PUBLIC).value();
        std::array<bool, N> expect{};
        expect[(std::size_t)(j - 1)] = true;
        if (bits != expect || mask != expect) fail("cube_at_index: wrong cube selected");
    }
    std::printf("PASS cube_at_index: selects the right cube for every j in [1,4]\n");

    // select_cube (composed) -------------------------------------------------

    // Same alice_r=13/bob_r=7/total=20 as the first sample_in_range case
    // (sample=11), over the same intervals=[0,8,12,16,20] (j=2, since
    // 8<11<=12) and cubes as above (cube 2 = "0100"/"0100").
    {
        RB a = sess.input<RB>(PUBLIC, bits_of_uint<WIDTH>(13));
        RB b = sess.input<RB>(PUBLIC, bits_of_uint<WIDTH>(7));
        W  t = sess.input<W>(PUBLIC, 20);
        std::array<W, M + 1> iw{};
        for (int i = 0; i < M + 1; ++i) iw[(std::size_t)i] = sess.input<W>(PUBLIC, intervals[(std::size_t)i]);

        CubeData<Ctx, N> result = select_cube<Ctx, N, M>(a, b, t, iw, cubes);
        std::array<bool, N> bits_got = sess.reveal(result.bits, PUBLIC).value();
        std::array<bool, N> mask_got = sess.reveal(result.mask, PUBLIC).value();

        if (bits_got != bits_of<N>("0100") || mask_got != bits_of<N>("0100"))
            fail("select_cube: composed pipeline gave the wrong result");
        std::printf("PASS select_cube: (sample=11, j=2) -> cube=\"0100\"/\"0100\"\n");
    }

    std::printf("select_cube_test: all checks passed\n");
    return 0;
}
