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
//      from step 3, and
//   5. samples a joint uniform random integer z in [1, total_weight]
//      (gadgets/select_cube.h's sample_in_range), picks the (1-indexed)
//      cube whose block z falls in via the T_0..T_M boundaries
//      (select_cube_index), and looks up that cube's bits/mask
//      (cube_at_index) -- see main() for why these are called directly
//      rather than through select_cube(), which only returns the cube.
//
// All of the above is revealed to both parties at the end. That's NOT the
// final protocol -- it's a placeholder so the whole pipeline can be
// smoke-tested end to end; a real circuit would consume all of it
// privately instead of leaking it. Expand this once that's built.
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
#include "gadgets/dnf_distribute.h"
#include "gadgets/cube_weight.h"
#include "gadgets/dnf_weight.h"
#include "gadgets/cube_intervals.h"
#include "gadgets/select_cube.h"

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

    // Sample a joint random integer in [1, total_weight], find which cube
    // it lands in (1-indexed), and look up that cube's bits/mask -- see
    // gadgets/select_cube.h for the wire-level math. Each party draws its
    // own real-entropy random bits locally (PRG() defaults to system
    // randomness, not a fixed seed) and feeds them in as private input --
    // that part is this session's job, not the gadget's, same as any
    // other private input.
    using RandBits = SampleBits<Ctx, VARS, PRODUCT>;
    array<bool, (size_t)TotalWidth> my_random_bits{};
    PRG().random_bool(my_random_bits.data(), TotalWidth);

    // select_cube() itself only returns the selected cube's bits/mask --
    // since this demo also wants to show the intermediate sample/index for
    // testing, it calls the granular sample_in_range/select_cube_index/
    // cube_at_index pieces directly instead (equivalent to what select_cube
    // does internally, just without discarding the intermediates).
    RandBits alice_r = sess.input<RandBits>(ALICE, my_random_bits);
    RandBits bob_r   = sess.input<RandBits>(BOB,   my_random_bits);
    TotalWeight sample = sample_in_range<Ctx, VARS, PRODUCT>(alice_r, bob_r, total);
    CubeIndex<Ctx, PRODUCT> cube_index = select_cube_index<Ctx, VARS, PRODUCT>(sample, intervals);
    CubeData<Ctx, VARS> selected = cube_at_index<Ctx, VARS, PRODUCT>(cube_index, conjunction);

    uint64_t total_out = sess.reveal(total, PUBLIC).value();
    uint64_t sample_out = sess.reveal(sample, PUBLIC).value();
    uint64_t cube_index_out = sess.reveal(cube_index, PUBLIC).value();
    array<bool, VARS> cube_bits_out = sess.reveal(selected.bits, PUBLIC).value();
    array<bool, VARS> cube_mask_out = sess.reveal(selected.mask, PUBLIC).value();
    array<uint64_t, PRODUCT + 1> intervals_out{};
    for (int i = 0; i < PRODUCT + 1; ++i)
        intervals_out[(size_t)i] = sess.reveal(intervals[(size_t)i], PUBLIC).value();

    sess.finalize();

    std::cout << (party == ALICE ? "[alice]" : "[bob]  ")
              << " file=" << path
              << "  total_weight=" << total_out
              << "  sample=" << sample_out
              << "  cube_index(1-indexed)=" << cube_index_out
              << "  cube_bits=";
    for (int i = 0; i < VARS; ++i) std::cout << (cube_bits_out[(size_t)i] ? '1' : '0');
    std::cout << "  cube_mask=";
    for (int i = 0; i < VARS; ++i) std::cout << (cube_mask_out[(size_t)i] ? '1' : '0');
    std::cout << "  intervals=[";
    for (int i = 0; i < PRODUCT + 1; ++i) {
        if (i) std::cout << ",";
        std::cout << intervals_out[(size_t)i];
    }
    std::cout << "]" << std::endl;

    return 0;
}
