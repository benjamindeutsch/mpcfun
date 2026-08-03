// Unit test for gadgets/dnf/dnf_distribute.h, run entirely in the clear via
// emp::ClearSession: no OT, no network, no garbling, single process. This
// checks the gate-level logic of conjoin() in isolation, before it's ever
// wired into the real SH2PCSession-based 2PC circuit -- since the gadget is
// Ctx-generic, plugging it into that circuit later needs no changes here.
//
// run_dnf_distribute_tests() is called from tests/run_tests.cpp's main(),
// the single entry point for every *_test.cpp under tests/ -- see that file.

#include "gadgets/dnf/dnf_distribute.h"
#include "tests/test_helpers.h"
#include "emp-tool/ir/session/clear_session.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace {

using namespace emp;
using namespace gadgets;

constexpr int N = 4;
using Ctx = ClearSession::ctx_t;

void check(ClearSession& sess, const char* name,
           const CircuitCube<Ctx, N>& a, const CircuitCube<Ctx, N>& b,
           const char* expect_bits, const char* expect_mask, bool expect_pad) {
    CircuitCube<Ctx, N> r = conjoin<Ctx, N>(a, b);
    std::array<bool, N> bits = sess.reveal(r.bits, PUBLIC).value();
    std::array<bool, N> mask = sess.reveal(r.mask, PUBLIC).value();
    bool pad = sess.reveal(r.pad, PUBLIC).value();

    if (bits != bits_of<N>(expect_bits) || mask != bits_of<N>(expect_mask) || pad != expect_pad) {
        std::fprintf(stderr, "FAIL %s\n", name);
        std::exit(1);
    }
    std::printf("PASS %s\n", name);
}

}  // namespace

int run_dnf_distribute_tests() {
    ClearSession sess;

    // Disjoint variables: A asserts x1 true, x2 false; B asserts x3 true.
    // No shared constraint, neither is padding -> conjunction just unions them.
    check(sess, "disjoint variables",
          make_cube<ClearSession, Ctx, N>(sess, "1000", "1100"),
          make_cube<ClearSession, Ctx, N>(sess, "0010", "0010"),
          "1010", "1110", false);

    // A cube conjoined with itself is itself.
    check(sess, "idempotent (same cube twice)",
          make_cube<ClearSession, Ctx, N>(sess, "1010", "1110"),
          make_cube<ClearSession, Ctx, N>(sess, "1010", "1110"),
          "1010", "1110", false);

    // Both constrain x1, and agree that it's true.
    check(sess, "overlap, agreeing value",
          make_cube<ClearSession, Ctx, N>(sess, "1000", "1000"),
          make_cube<ClearSession, Ctx, N>(sess, "1100", "1100"),
          "1100", "1100", false);

    // Both constrain x1, but disagree (A: x1 true, B: x1 false) -> the
    // conjunction is unsatisfiable, so the result is padding: bits/mask are
    // pinned to 1^N/0^N regardless of what the raw per-bit mux would say.
    check(sess, "overlap, conflicting value -> padding",
          make_cube<ClearSession, Ctx, N>(sess, "1000", "1000"),
          make_cube<ClearSession, Ctx, N>(sess, "0100", "1100"),
          "1111", "0000", true);

    // The empty cube (no literals -- always true) conjoined with anything
    // is just that thing.
    check(sess, "empty cube is an identity",
          make_cube<ClearSession, Ctx, N>(sess, "0000", "0000"),
          make_cube<ClearSession, Ctx, N>(sess, "1010", "1110"),
          "1010", "1110", false);

    // Padding is contagious: if either input is already padding, the result
    // is padding too, even with no bit-level disagreement at all (this B's
    // mask is all-0, so nothing to disagree on).
    check(sess, "either input already padding -> padding",
          make_cube<ClearSession, Ctx, N>(sess, "1010", "1110"),
          make_cube<ClearSession, Ctx, N>(sess, "1111", "0000", /*pad=*/true),
          "1111", "0000", true);

    // conjoin_dnf: 2 cubes x 2 cubes -> 4 results, mixing an ordinary and a
    // contradictory (-> padding) pairing.
    std::array<CircuitCube<Ctx, N>, 2> d1 = {make_cube<ClearSession, Ctx, N>(sess, "1000", "1000"),   // x1
                                              make_cube<ClearSession, Ctx, N>(sess, "0100", "0100")}; // x2
    std::array<CircuitCube<Ctx, N>, 2> d2 = {make_cube<ClearSession, Ctx, N>(sess, "1000", "1000"),   // x1 (agrees with d1[0])
                                              make_cube<ClearSession, Ctx, N>(sess, "0000", "1000")}; // ~x1 (conflicts with d1[0])
    std::array<CircuitCube<Ctx, N>, 4> cross = conjoin_dnf<Ctx, N, 2>(d1, d2);
    // d1[0] /\ d2[0]: x1 /\ x1 -> ordinary, bits=1000
    assert(!sess.reveal(cross[0].pad, PUBLIC).value());
    assert(sess.reveal(cross[0].bits, PUBLIC).value() == bits_of<N>("1000"));
    // d1[0] /\ d2[1]: x1 /\ ~x1 -> contradiction -> padding
    assert(sess.reveal(cross[1].pad, PUBLIC).value());
    assert(sess.reveal(cross[1].bits, PUBLIC).value() == bits_of<N>("1111"));
    assert(sess.reveal(cross[1].mask, PUBLIC).value() == bits_of<N>("0000"));
    // d1[1] /\ d2[0]: x2 /\ x1 -> ordinary, disjoint, bits=1100
    assert(!sess.reveal(cross[2].pad, PUBLIC).value());
    assert(sess.reveal(cross[2].bits, PUBLIC).value() == bits_of<N>("1100"));
    // d1[1] /\ d2[1]: x2 /\ ~x1 -> ordinary, disjoint, bits=0100
    assert(!sess.reveal(cross[3].pad, PUBLIC).value());
    assert(sess.reveal(cross[3].bits, PUBLIC).value() == bits_of<N>("0100"));
    std::printf("PASS conjoin_dnf cross product\n");

    std::printf("dnf_distribute_test: all checks passed\n");
    return 0;
}
