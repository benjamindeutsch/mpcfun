// Interactive two-party demo of the DNF-intersection-counting pipeline
// (pipeline/karp_luby_pipeline.h): each party's private input is a path to
// its own DIMACS-DNF file. Parsing happens locally, in the clear, before
// either party touches the network -- it's data prep, not a circuit. See
// pipeline/karp_luby_pipeline.h's own file comment for what the circuit
// itself does (conjunction -> weighting -> K Karp-Luby trials -> reveal);
// this file is just the CLI/session/output wrapper around it.
//
// K here is a small, fixed constant for a fast interactive smoke test --
// nowhere near enough trials for a real epsilon/confidence guarantee (see
// gadgets/karp_luby/karp_luby_estimate.h's karp_luby_trials() for how to
// pick a real K from a target epsilon). src/bench/bench_karp_luby.cpp uses
// that instead, for actual benchmarking.
//
// VARS/CUBES are the fixed capacity every DNF gets padded/validated against
// (see dimacs_dnf::parse) -- they're compile-time constants shared by both
// parties, not read from either file, because the circuit's size (and
// hence the number of variables/cubes each party's DNF can mention) has to
// be public in MPC; that's the whole reason dimacs_dnf::parse pads to a
// fixed capacity instead of just sizing to whatever's in the file.
//
// Only the pipeline's final raw Karp-Luby numerator is revealed --
// everything else (the total weight, the interval boundaries, every
// trial's sample/selected cube/assignment/satisfied count) stays private,
// known to neither party. The Karp-Luby estimate itself is (the revealed
// value) / (K * gadgets::lookup_scale<PRODUCT>()): both K and the lookup
// scale are public compile-time constants, so that division happens for
// free in plaintext after reveal, rather than spending circuit gates on it.
//
// Usage:
//   ./sh2pc_demo <party: 1=ALICE, 2=BOB> <path-to-dimacs-dnf-file>
//
// Alice listens on a fixed localhost port (default 12345, override with the
// EMP_PORT env var), Bob connects to it (override the address with
// EMP_PEER_IP).

#include "pipeline/karp_luby_pipeline.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace emp;
using namespace gadgets;

constexpr int VARS = 4;
constexpr int CUBES = 2;
constexpr int PRODUCT = CUBES * CUBES;
constexpr int K = 3;  // small fixed K for a fast interactive smoke test -- see file comment above

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <party: 1=ALICE, 2=BOB> <path-to-dimacs-dnf-file>\n";
        return 1;
    }

    int party = parse_party(argv);
    std::string path = argv[2];

    // Local data prep: each party parses only its OWN file. Plain stdlib
    // parsing (utils/dimacs_dnf.h has no emp-tool dependency) -- no network
    // involved yet, so a malformed file fails fast before either party
    // starts listening/connecting.
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

    // unsigned __int128, not uint64_t: run_karp_luby_pipeline's return
    // type has to accommodate every VARS bench_karp_luby.cpp's sweep uses
    // (up to 100 bits at VARS=64), even though this demo's own VARS=4
    // result always fits safely in far fewer bits -- see
    // pipeline/karp_luby_pipeline.h's top comment.
    unsigned __int128 estimate_raw_out = run_karp_luby_pipeline<VARS, CUBES, K>(sess, my_dnf);

    sess.finalize();

    // Un-scale the raw estimate: divide by K * lookup_scale<PRODUCT>(),
    // both public compile-time constants, so this costs nothing in the
    // circuit -- see gadgets/karp_luby/karp_luby_estimate.h.
    uint64_t scale = lookup_scale<PRODUCT>();
    double karp_luby_estimate_value = (double)estimate_raw_out / (double)((uint64_t)K * scale);

    std::string who = (party == ALICE) ? "[alice]" : "[bob]  ";
    std::cout << who << " file=" << path
              << "  karp_luby_estimate_raw=" << (uint64_t)estimate_raw_out
              << "  (K=" << K << ", scale=" << scale << ")"
              << "  karp_luby_estimate=" << karp_luby_estimate_value << std::endl;

    return 0;
}
