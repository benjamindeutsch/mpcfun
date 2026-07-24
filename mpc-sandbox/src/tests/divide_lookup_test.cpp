// Unit tests for gadgets/divide_lookup.h, run entirely in the clear via
// emp::ClearSession: no OT, no network, no garbling, single process --
// same approach as select_cube_test.cpp.
//
// run_divide_lookup_tests() is called from tests/run_tests.cpp's main(),
// the single entry point for every *_test.cpp under tests/ -- see that
// file.

#include "gadgets/divide_lookup.h"
#include "emp-tool/ir/session/clear_session.h"

#include <cstdio>
#include <cstdlib>

namespace {

using namespace emp;
using namespace gadgets;

constexpr int M = 4;
using Ctx = ClearSession::ctx_t;
using Count = SatisfiedCount<Ctx, M>;

// lcm(1,2,3,4) = 12, computed at compile time -- checked here against the
// hand computation so a change to lookup_scale's algorithm can't silently
// drift from what the rest of this test assumes.
static_assert(lookup_scale<M>() == 12, "lcm(1,2,3,4) should be 12");

void check(ClearSession& sess, const char* name, uint64_t index, uint64_t expect) {
    Count idx = sess.input<Count>(PUBLIC, index);
    uint64_t got = sess.reveal(divide_lookup<Ctx, M>(idx), PUBLIC).value();
    if (got != expect) {
        std::fprintf(stderr, "FAIL %s (got %llu, expected %llu)\n", name,
                      (unsigned long long)got, (unsigned long long)expect);
        std::exit(1);
    }
    std::printf("PASS %s\n", name);
}

}  // namespace

int run_divide_lookup_tests() {
    ClearSession sess;

    // SCALE = lcm(1,2,3,4) = 12; every entry is SCALE/index, exactly.
    check(sess, "divide_lookup(1) = 12/1 = 12", 1, 12);
    check(sess, "divide_lookup(2) = 12/2 = 6", 2, 6);
    check(sess, "divide_lookup(3) = 12/3 = 4", 3, 4);
    check(sess, "divide_lookup(4) = 12/4 = 3", 4, 3);

    std::printf("divide_lookup_test: all checks passed\n");
    return 0;
}
