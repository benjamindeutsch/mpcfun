// Unit tests for gadgets/karp_luby/karp_luby_estimate.h, run entirely in
// the clear via emp::ClearSession: no OT, no network, no garbling, single
// process -- same approach as select_cube_test.cpp.
//
// run_karp_luby_estimate_tests() is called from tests/run_tests.cpp's
// main(), the single entry point for every *_test.cpp under tests/ -- see
// that file.

#include "gadgets/karp_luby/karp_luby_estimate.h"
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

// karp_luby_trials(vars, epsilon, delta) = ceil((1/delta)*(vars^2-1)^2/epsilon^2),
// checked against three hand-computed cases: two at delta=0.25 (probability
// >= 3/4, this function's original form, constant folds to 4) that are
// exactly representable (no rounding): vars=4,epsilon=0.5 -> vars^2-1=15,
// 4*15^2/0.5^2 = 4*225/0.25 = 3600; vars=2,epsilon=1.0 -> vars^2-1=3,
// 4*3^2/1^2 = 36. The third matches ApproxMC's own defaults
// (epsilon=0.8, delta=0.2 -- see src/bench/bench_karp_luby.cpp):
// vars=4 -> vars^2-1=15, (1/0.2)*15^2/0.8^2 = 5*225/0.64 = 1757.8125,
// rounded up to 1758.
static_assert(karp_luby_trials(4, 0.5, 0.25) == 3600, "K for vars=4, epsilon=0.5, delta=0.25 should be 3600");
static_assert(karp_luby_trials(2, 1.0, 0.25) == 36, "K for vars=2, epsilon=1.0, delta=0.25 should be 36");
static_assert(karp_luby_trials(4, 0.8, 0.2) == 1758, "K for vars=4, epsilon=0.8, delta=0.2 (ApproxMC defaults) should be 1758");

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
    // divide_lookup(1)=65536, divide_lookup(2)=32768, divide_lookup(4)=16384
    // (3 independent trials with satisfied_count = 1, 2, 4; all three are
    // exact -- SCALE=65536 divides evenly by 1, 2, and 4):
    // 20 * (65536+32768+16384) = 20*114688 = 2293760.
    check(sess, "weight=20, reciprocals=[65536,32768,16384] -> 20*114688=2293760",
          20, {65536, 32768, 16384}, 2293760);

    // All three trials landed on satisfied_count=1 -> every reciprocal is
    // the max table entry, SCALE=65536: 1 * (65536*3) = 196608.
    check(sess, "weight=1, reciprocals=[65536,65536,65536] -> 1*196608=196608",
          1, {65536, 65536, 65536}, 196608);

    // weight=0 (degenerate, e.g. an all-padding conjunction) -> estimate
    // is 0 regardless of the reciprocals.
    check(sess, "weight=0 -> estimate=0", 0, {65536, 32768, 16384}, 0);

    std::printf("karp_luby_estimate_test: all checks passed\n");
    return 0;
}
