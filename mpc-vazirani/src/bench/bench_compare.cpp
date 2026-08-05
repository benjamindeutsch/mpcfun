// bench_compare: times both the Vazirani and Karp-Luby two-party circuits
// (pipeline/vazirani_pipeline.h, pipeline/karp_luby_pipeline.h) across the
// same (VARS,CUBES) sweep and epsilon/delta as bench_vazirani.cpp, printing
// both algorithms' summary lines and per-gadget breakdowns back-to-back for
// each size -- the side-by-side comparison bench_vazirani.cpp alone can't
// give. Built entirely on bench/bench_common.h's generic
// run_one<Adapter,VARS,CUBES>() sweep runner plus each pipeline's own Adapter
// (VaziraniAdapter, KarpLubyAdapter) -- this file itself has no
// algorithm-specific logic.
//
// Usage:
//   ./bench_compare <party: 1=ALICE, 2=BOB> [max_overlap] [seed]
//
// Same two-party launch convention as bench_vazirani: Alice listens on a
// fixed localhost port (default 12345, override with EMP_PORT), Bob
// connects (override the address with EMP_PEER_IP). Same random-DNF-pair
// generation and pepin ground-truth sanity check as bench_vazirani.cpp
// (see that file's top comment and bench/bench_common.h's run_one) --
// both algorithms here run against the *same* generated pair at each
// sweep point (run_both below calls run_one twice with the same
// VARS/CUBES/max_overlap/seed), so their estimates are directly
// comparable to each other and to the one pepin_ground_truth line.

#include "pipeline/vazirani_pipeline.h"
#include "pipeline/karp_luby_pipeline.h"
#include "bench/bench_common.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

using namespace emp;

// Same ApproxMC defaults as bench_vazirani.cpp, so both sweeps are
// directly comparable.
constexpr double EPSILON = 0.8;
constexpr double DELTA = 0.2;

// VARS and CUBES are independent template parameters (see
// bench/bench_common.h's run_one) -- this sweep just happens to hold
// CUBES=VARS at every point, same as bench_vazirani.cpp's.
template <int VARS, int CUBES>
void run_both(SH2PCSession& sess, NetIO* io, int party, int max_overlap, uint64_t seed, const std::string& who) {
    bench::run_one<VaziraniAdapter, VARS, CUBES, EPSILON, DELTA>(sess, io, party, max_overlap, seed, who);
    bench::run_one<KarpLubyAdapter, VARS, CUBES, EPSILON, DELTA>(sess, io, party, max_overlap, seed, who);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <party: 1=ALICE, 2=BOB> [max_overlap] [seed]\n";
        return 1;
    }

    bench::raise_stack_limit();

    int party = parse_party(argv);
    int max_overlap = (argc > 2) ? std::stoi(argv[2]) : std::numeric_limits<int>::max();
    uint64_t seed = (argc > 3) ? std::stoull(argv[3]) : 42;

    int port = peer_port();
    auto io = (party == ALICE) ? NetIO::listen(port)
                                : NetIO::connect(peer_ip(), port);

    SH2PCSession sess(io.get(), party);
    std::string who = (party == ALICE) ? "[alice]" : "[bob]  ";

    run_both<4, 4>(sess, io.get(), party, max_overlap, seed, who);
    run_both<8, 8>(sess, io.get(), party, max_overlap, seed, who);
    run_both<16, 16>(sess, io.get(), party, max_overlap, seed, who);
    run_both<32, 32>(sess, io.get(), party, max_overlap, seed, who);
    run_both<64, 64>(sess, io.get(), party, max_overlap, seed, who);

    sess.finalize();
    return 0;
}
