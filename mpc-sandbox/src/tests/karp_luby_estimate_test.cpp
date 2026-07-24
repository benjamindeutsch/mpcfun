// Unit tests for gadgets/karp_luby_estimate.h, run entirely in the clear
// via emp::ClearSession: no OT, no network, no garbling, single process --
// same approach as select_cube_test.cpp.
//
// run_karp_luby_estimate_tests() is called from tests/run_tests.cpp's
// main(), the single entry point for every *_test.cpp under tests/ -- see
// that file.

#include "gadgets/karp_luby_estimate.h"
#include "emp-tool/ir/session/clear_session.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {

using namespace emp;
using namespace gadgets;

constexpr int N = 4;
constexpr int M = 4;
constexpr int K = 3;
using Ctx = ClearSession::ctx_t;
using W = DnfWeight<Ctx, N, M>;
using R = DivideLookupResult<Ctx, M>;

void check(ClearSession& sess, const char* name, uint64_t weight_val,
           const std::array<uint64_t, K>& reciprocal_vals, uint64_t expect) {
    W weight = sess.input<W>(PUBLIC, weight_val);
    std::array<R, K> reciprocals{};
    for (int t = 0; t < K; ++t) reciprocals[(std::size_t)t] = sess.input<R>(PUBLIC, reciprocal_vals[(std::size_t)t]);

    uint64_t got = sess.reveal(karp_luby_estimate<Ctx, N, M, K>(weight, reciprocals), PUBLIC).value();
    if (got != expect) {
        std::fprintf(stderr, "FAIL %s (got %llu, expected %llu)\n", name,
                      (unsigned long long)got, (unsigned long long)expect);
        std::exit(1);
    }
    std::printf("PASS %s\n", name);
}

}  // namespace

int run_karp_luby_estimate_tests() {
    ClearSession sess;

    // weight=20 (this pipeline's real dnf_weight), reciprocals from
    // divide_lookup(1)=12, divide_lookup(2)=6, divide_lookup(4)=3 (3
    // independent trials with satisfied_count = 1, 2, 4):
    // 20 * (12+6+3) = 20*21 = 420.
    check(sess, "weight=20, reciprocals=[12,6,3] -> 20*21=420", 20, {12, 6, 3}, 420);

    // All three trials landed on satisfied_count=1 -> every reciprocal is
    // the max table entry, SCALE=12: 1 * (12+12+12) = 36.
    check(sess, "weight=1, reciprocals=[12,12,12] -> 1*36=36", 1, {12, 12, 12}, 36);

    // weight=0 (degenerate, e.g. an all-padding conjunction) -> estimate
    // is 0 regardless of the reciprocals.
    check(sess, "weight=0 -> estimate=0", 0, {12, 6, 4}, 0);

    std::printf("karp_luby_estimate_test: all checks passed\n");
    return 0;
}
