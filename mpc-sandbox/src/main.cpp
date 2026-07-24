// End-to-end DNF-intersection-counting pipeline: each party's private input
// is a path to its own DIMACS-DNF file (see utils/dimacs_dnf.h). Parsing
// happens locally, in the clear, before either party touches the network --
// it's data prep, not a circuit. The parsed cubes then become each party's
// private input to the 2PC circuit, which:
//   1. computes the cubes of the conjunction of the two DNFs
//      (gadgets/dnf_distribute.h's conjoin_dnf),
//   2. computes each resulting cube's weight (gadgets/cube_weight.h),
//   3. sums those into the conjunction's total satisfying-assignment count
//      (gadgets/dnf_weight.h), and
//   4. computes the CUBES*CUBES+1 interval boundaries T_0..T_M over those
//      weights (gadgets/cube_intervals.h), with T_M equal to the total
//      from step 3, then
//   5. runs K independent trials of: sample a joint uniform random
//      integer z in [1, total_weight], pick the cube whose block z falls
//      in, and look up its bits/mask (gadgets/select_cube.h), extend it
//      into a full random satisfying assignment (gadgets/
//      random_assignment.h), count how many of the CUBES*CUBES conjunction
//      cubes that assignment satisfies (gadgets/count_satisfied_cubes.h),
//      and look up a fixed-point 1/count via an oblivious table instead of
//      a division circuit (gadgets/divide_lookup.h), and finally
//   6. sums the K reciprocals and multiplies by the total from step 3
//      (gadgets/karp_luby_estimate.h) -- the raw numerator of the
//      Karp-Luby estimator for the DNF's *true* satisfying-assignment
//      count (which can be less than step 3's total, since these
//      particular conjunction cubes aren't guaranteed pairwise disjoint).
//
// Only that final raw numerator is revealed -- everything else (the total
// weight, the interval boundaries, every trial's sample/selected
// cube/assignment/satisfied count) stays private, known to neither party.
// The Karp-Luby estimate itself is (the revealed value) / (K *
// gadgets::lookup_scale<PRODUCT>()): both K and the lookup scale are
// public compile-time constants, so that division happens for free in
// plaintext after reveal, rather than spending circuit gates on it.
//
// VARS/CUBES are the fixed capacity every DNF gets padded/validated against
// (see dimacs_dnf::parse) -- they're compile-time constants shared by both
// parties, not read from either file, because the circuit's size (and
// hence the number of variables/cubes each party's DNF can mention) has to
// be public in MPC; that's the whole reason dimacs_dnf::parse pads to a
// fixed capacity instead of just sizing to whatever's in the file.
//
// Usage:
//   ./sh2pc_demo <party: 1=ALICE, 2=BOB> <path-to-dimacs-dnf-file>
//
// Alice listens on a fixed localhost port (default 12345, override with the
// EMP_PORT env var), Bob connects to it (override the address with
// EMP_PEER_IP).

#include "emp-sh2pc/emp-sh2pc.h"
#include "utils/dimacs_dnf.h"
#include "gadgets/circuit_cube.h"
#include "gadgets/dnf/dnf_distribute.h"
#include "gadgets/dnf/cube_weight.h"
#include "gadgets/dnf/dnf_weight.h"
#include "gadgets/dnf/cube_intervals.h"
#include "gadgets/karp_luby/select_cube.h"
#include "gadgets/karp_luby/random_assignment.h"
#include "gadgets/karp_luby/count_satisfied_cubes.h"
#include "gadgets/karp_luby/divide_lookup.h"
#include "gadgets/karp_luby/karp_luby_estimate.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

using std::array;
using std::size_t;

using namespace emp;
using namespace gadgets;

constexpr int VARS = 4;
constexpr int CUBES = 2;
constexpr int PRODUCT = CUBES * CUBES;
constexpr int K = 3;  // number of independent Karp-Luby trials

using Ctx = SH2PCSession::ctx_t;
using BV  = BitVec_T<Ctx, VARS>;

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

    // Feed each party's own parsed cubes in as that party's private input.
    // Both parties run both loops -- as with any secret input in this
    // toolkit, only the named owner's argument is actually used (see
    // SH2PCSession::input), so each process just passes its own local
    // my_dnf data regardless of which owner it's calling for.
    array<CircuitCube<Ctx, VARS>, CUBES> alice_cubes{};
    array<CircuitCube<Ctx, VARS>, CUBES> bob_cubes{};
    for (int i = 0; i < CUBES; ++i) {
        const auto& mine = my_dnf.cubes[(size_t)i];
        alice_cubes[(size_t)i] = CircuitCube<Ctx, VARS>{
            sess.input<BV>(ALICE, mine.bits),
            sess.input<BV>(ALICE, mine.mask),
            sess.input<Bit_T<Ctx>>(ALICE, mine.pad),
        };
        bob_cubes[(size_t)i] = CircuitCube<Ctx, VARS>{
            sess.input<BV>(BOB, mine.bits),
            sess.input<BV>(BOB, mine.mask),
            sess.input<Bit_T<Ctx>>(BOB, mine.pad),
        };
    }

    array<CircuitCube<Ctx, VARS>, PRODUCT> conjunction =
        conjoin_dnf<Ctx, VARS, CUBES>(alice_cubes, bob_cubes);
    array<CubeWeight<Ctx, VARS>, PRODUCT> weights =
        cube_weights<Ctx, VARS, PRODUCT>(conjunction);

    using TotalWeight = DnfWeight<Ctx, VARS, PRODUCT>;
    constexpr int TotalWidth = TotalWeight::width();

    TotalWeight total = dnf_weight<Ctx, VARS, PRODUCT>(weights);
    array<TotalWeight, PRODUCT + 1> intervals =
        cube_intervals<Ctx, VARS, PRODUCT>(weights);

    // K independent Karp-Luby trials. Each trial draws its own fresh joint
    // randomness (both for select_cube's sampling step and for
    // random_assignment's free-variable fill), so the K samples are
    // independent -- required for the estimator's variance to actually
    // shrink with K. Nothing about any trial is revealed -- only the final
    // combined estimate is, below.
    using RandBits = SampleBits<Ctx, VARS, PRODUCT>;

    array<DivideLookupResult<Ctx, PRODUCT>, K> reciprocals{};

    for (int t = 0; t < K; ++t) {
        array<bool, (size_t)TotalWidth> my_random_bits{};
        PRG().random_bool(my_random_bits.data(), TotalWidth);
        RandBits alice_r = sess.input<RandBits>(ALICE, my_random_bits);
        RandBits bob_r   = sess.input<RandBits>(BOB,   my_random_bits);
        CubeData<Ctx, VARS> selected =
            select_cube<Ctx, VARS, PRODUCT>(alice_r, bob_r, total, intervals, conjunction);

        // Extend the selected cube into a full random satisfying
        // assignment (gadgets/random_assignment.h): a second, independent
        // joint random bitstring (same free-XOR construction, drawn/fed
        // the same way) fills in whatever the cube leaves unconstrained.
        array<bool, VARS> my_assignment_bits{};
        PRG().random_bool(my_assignment_bits.data(), VARS);
        BV assignment_alice_r = sess.input<BV>(ALICE, my_assignment_bits);
        BV assignment_bob_r   = sess.input<BV>(BOB,   my_assignment_bits);
        BV assignment = random_assignment<Ctx, VARS>(selected, assignment_alice_r, assignment_bob_r);

        SatisfiedCount<Ctx, PRODUCT> satisfied_count =
            count_satisfied_cubes<Ctx, VARS, PRODUCT>(assignment, conjunction);
        reciprocals[(size_t)t] = divide_lookup<Ctx, PRODUCT>(satisfied_count);
    }

    KarpLubyEstimate<Ctx, VARS, PRODUCT, K> estimate =
        karp_luby_estimate<Ctx, VARS, PRODUCT, K>(total, reciprocals);

    uint64_t estimate_raw_out = sess.reveal(estimate, PUBLIC).value();

    sess.finalize();

    // Un-scale the raw estimate: divide by K * lookup_scale<PRODUCT>(),
    // both public compile-time constants, so this costs nothing in the
    // circuit -- see gadgets/karp_luby_estimate.h.
    uint64_t scale = lookup_scale<PRODUCT>();
    double karp_luby_estimate_value = (double)estimate_raw_out / (double)((uint64_t)K * scale);

    std::string who = (party == ALICE) ? "[alice]" : "[bob]  ";
    std::cout << who << " file=" << path
              << "  karp_luby_estimate_raw=" << estimate_raw_out
              << "  (K=" << K << ", scale=" << scale << ")"
              << "  karp_luby_estimate=" << karp_luby_estimate_value << std::endl;

    return 0;
}
