// Unit tests for gadgets/karp_luby/divide_lookup.h, run entirely in the
// clear via emp::ClearSession: no OT, no network, no garbling, single
// process -- same approach as select_cube_test.cpp.
//
// run_divide_lookup_tests() is called from tests/run_tests.cpp's main(),
// the single entry point for every *_test.cpp under tests/ -- see that
// file.

#include "gadgets/karp_luby/divide_lookup.h"
#include "emp-tool/ir/session/clear_session.h"

#include <cstdio>
#include <cstdlib>

namespace {

using namespace emp;
using namespace gadgets;

constexpr int M = 4;
using Ctx = ClearSession::ctx_t;
using Count = SatisfiedCount<Ctx, M>;

// SCALE = 2^16 = 65536, checked here against the hand computation so a
// change to kDivideLookupScaleBits can't silently drift from what the
// rest of this test assumes.
static_assert(lookup_scale<M>() == 65536, "SCALE should be 2^16 = 65536");

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

    // SCALE = 65536; every entry is round(SCALE/index) -- exact for
    // index=1,2,4 (which divide SCALE evenly), rounded for index=3
    // (65536/3 = 21845.33... -> 21845).
    check(sess, "divide_lookup(1) = round(65536/1) = 65536", 1, 65536);
    check(sess, "divide_lookup(2) = round(65536/2) = 32768", 2, 32768);
    check(sess, "divide_lookup(3) = round(65536/3) = 21845", 3, 21845);
    check(sess, "divide_lookup(4) = round(65536/4) = 16384", 4, 16384);

    std::printf("divide_lookup_test: all checks passed\n");
    return 0;
}
