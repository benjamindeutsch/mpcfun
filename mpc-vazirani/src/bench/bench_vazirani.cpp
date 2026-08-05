// bench_vazirani: times the real two-party Vazirani circuit
// (pipeline/vazirani_pipeline.h) across a sweep of (VARS,CUBES) sizes
// (VARS=CUBES, doubling from 4 to 32), each at a *fixed*, meaningful
// epsilon/delta -- ApproxMC's own defaults, epsilon=0.8 (tolerance) and
// delta=0.2 (failure probability, i.e. success probability >= 1-delta =
// 0.8).
//
// K = gadgets::vazirani_trials(CUBES, epsilon, delta), which (after an
// earlier bug -- see that function's own comment) is:
//
//   K = ceil(min(1/delta, 3*ln(2/delta)) * (CUBES^2 - 1) / epsilon^2)
//
// linear in CUBES^2 (the number of cubes in the conjoined DNF), not
// quadratic in it -- that linearity is the actual Vazirani theorem. An
// earlier, incorrect version of vazirani_trials was quadratic in
// (VARS^2-1) instead, which made K explode as O(VARS^4) and made anything
// past VARS=16 intractable (billions of trials, hours to months). The
// corrected, linear formula is cheap enough to sweep all the way to
// VARS=32 in under two minutes total, measured live:
//
//   CUBES   M=CUBES^2   K       K*M (work)   measured elapsed
//   4       16          118     1888         34.48 ms
//   8       64          493     31552        480.12 ms
//   16      256         1993    510208       5.88 s
//   32      1024        7993    8184832      1.40 min
//
// (`c = min(1/delta, 3*ln(2/delta))` happens to be `5` -- i.e. Chebyshev,
// not the Chernoff alternative -- at this epsilon/delta; see
// vazirani_trials' own comment for when each wins.)
//
// It follows emp-toolkit's own benchmark conventions (see
// emp-tool/docs/benchmark_conventions.md): the timed region is wrapped in
// clock_start()/time_from() (emp-tool/runtime/core/utils.h), isolated per
// config from unrelated setup, and per-config network bytes/rounds are
// captured via the same before/after
// io->send_counter/io->recv_counter/io->rounds snapshot
// emp-ot/bench/bench.h's time_ot() uses -- NetIO's own end-of-process
// printout only gives a session-wide total, since one connection is
// reused across every config here.
//
// Usage:
//   ./bench_vazirani <party: 1=ALICE, 2=BOB> [max_overlap] [seed]
//
// Same two-party launch convention as sh2pc_demo: Alice listens on a fixed
// localhost port (default 12345, override with EMP_PORT), Bob connects
// (override the address with EMP_PEER_IP). Rather than reading a fixed DNF
// file, each sweep point generates a fresh random Alice/Bob pair sized
// exactly to that point's VARS/CUBES (see utils/random_dnf.h) --
// max_overlap caps the number of literals that may agree between any cube
// of Alice's and any cube of Bob's (clamped to VARS at each point;
// defaults to VARS itself, i.e. unrestricted, if not given). seed (default
// 42) makes the sweep reproducible; both processes derive the identical
// pair from the same public VARS/CUBES/max_overlap/seed without
// exchanging anything (see bench/bench_common.h's run_one). Alice's
// process also shells out to the bundled `pepin` approximate DNF model
// counter on the plaintext conjunction, printed as `pepin_ground_truth`
// alongside the protocol's own revealed estimate as a sanity check (see
// bench/ground_truth.h).

#include "pipeline/vazirani_pipeline.h"
#include "bench/bench_common.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

using namespace emp;

// ApproxMC's own defaults: tolerance epsilon=0.8, failure probability
// delta=0.2 (success probability >= 0.8) -- see this file's top comment.
constexpr double EPSILON = 0.8;
constexpr double DELTA = 0.2;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <party: 1=ALICE, 2=BOB> [max_overlap] [seed]\n";
        return 1;
    }

    // None of this sweep's current sizes actually need more than the 8 MiB
    // Linux default stack any more (vazirani_trials' K values are all
    // small now -- see this file's top comment), but pipeline/
    // vazirani_pipeline.h's conjunction/weights/intervals locals scale
    // with M=CUBES^2 regardless of K, and would need this again well
    // before a hypothetical VARS=64 sweep point. See bench_common.h's
    // raise_stack_limit() for why this is safe to do here, after the
    // process has already started.
    bench::raise_stack_limit();

    int party = parse_party(argv);
    // max_overlap: clamped to each sweep point's own VARS by run_one, so a
    // large default (INT_MAX) here just means "unrestricted" at every
    // size, not literally VARS-at-4's cap leaking into VARS=64's point.
    int max_overlap = (argc > 2) ? std::stoi(argv[2]) : std::numeric_limits<int>::max();
    uint64_t seed = (argc > 3) ? std::stoull(argv[3]) : 42;

    int port = peer_port();
    auto io = (party == ALICE) ? NetIO::listen(port)
                                : NetIO::connect(peer_ip(), port);

    SH2PCSession sess(io.get(), party);
    std::string who = (party == ALICE) ? "[alice]" : "[bob]  ";

    // VARS and CUBES are independent template parameters (the pipeline
    // underneath doesn't require them equal); this sweep just happens to
    // hold CUBES=VARS at every point.
    bench::run_one<VaziraniAdapter, 4, 4, EPSILON, DELTA>(sess, io.get(), party, max_overlap, seed, who);
    bench::run_one<VaziraniAdapter, 8, 8, EPSILON, DELTA>(sess, io.get(), party, max_overlap, seed, who);
    bench::run_one<VaziraniAdapter, 16, 16, EPSILON, DELTA>(sess, io.get(), party, max_overlap, seed, who);
    bench::run_one<VaziraniAdapter, 32, 32, EPSILON, DELTA>(sess, io.get(), party, max_overlap, seed, who);
    bench::run_one<VaziraniAdapter, 64, 64, EPSILON, DELTA>(sess, io.get(), party, max_overlap, seed, who);

    sess.finalize();
    return 0;
}
