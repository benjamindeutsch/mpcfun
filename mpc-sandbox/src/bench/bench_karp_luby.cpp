// bench_karp_luby: times the real two-party Karp-Luby circuit
// (pipeline/karp_luby_pipeline.h) at the K required for a target relative
// error EPSILON with probability >= 3/4 (see
// gadgets/karp_luby/karp_luby_estimate.h's karp_luby_trials()) --
// following emp-toolkit's own benchmark conventions (see
// emp-tool/docs/benchmark_conventions.md): the timed region is wrapped in
// clock_start()/time_from() (emp-tool/runtime/core/utils.h), isolated from
// unrelated setup (DIMACS parsing, session/network setup) so the reported
// time reflects only the protocol call. Network stats (bytes sent/received,
// rounds, flushes) print automatically from NetIO's own destructor -- no
// extra instrumentation needed for those (see NetIO(..., quiet=false), the
// default both here and in sh2pc_demo).
//
// Usage:
//   ./bench_karp_luby <party: 1=ALICE, 2=BOB> <path-to-dimacs-dnf-file>
//
// Same two-party launch convention as sh2pc_demo: Alice listens on a fixed
// localhost port (default 12345, override with EMP_PORT), Bob connects
// (override the address with EMP_PEER_IP).

#include "pipeline/karp_luby_pipeline.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace emp;
using namespace gadgets;

constexpr int VARS = 4;
constexpr int CUBES = 2;
constexpr int PRODUCT = CUBES * CUBES;
constexpr double EPSILON = 0.2;  // target relative error; success probability >= 3/4
constexpr int K = karp_luby_trials(VARS, EPSILON);

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <party: 1=ALICE, 2=BOB> <path-to-dimacs-dnf-file>\n";
        return 1;
    }

    int party = parse_party(argv);
    std::string path = argv[2];

    dimacs_dnf::Dnf<VARS, CUBES> my_dnf;
    try {
        my_dnf = dimacs_dnf::parse<VARS, CUBES>(path);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    int port = peer_port();
    auto io = (party == ALICE) ? NetIO::listen(port)
                                : NetIO::connect(peer_ip(), port);

    SH2PCSession sess(io.get(), party);

    // Only the protocol call itself is timed -- not the DIMACS parse above
    // (local, pre-network data prep) or the session/NetIO setup (connection
    // handshake), matching emp-tool's own bench convention of not billing
    // unrelated setup work to the timed call.
    auto start = clock_start();
    uint64_t estimate_raw_out = run_karp_luby_pipeline<VARS, CUBES, K>(sess, my_dnf);
    double elapsed_us = time_from(start);

    sess.finalize();

    uint64_t scale = lookup_scale<PRODUCT>();
    double karp_luby_estimate_value = (double)estimate_raw_out / (double)((uint64_t)K * scale);

    std::string who = (party == ALICE) ? "[alice]" : "[bob]  ";
    std::cout << who << " file=" << path
              << "  VARS=" << VARS << " CUBES=" << CUBES
              << "  epsilon=" << EPSILON << " K=" << K
              << "  karp_luby_estimate=" << karp_luby_estimate_value
              << "  elapsed=" << (elapsed_us / 1000.0) << " ms" << std::endl;

    return 0;
}
