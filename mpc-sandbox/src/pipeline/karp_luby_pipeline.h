// run_karp_luby_pipeline: the real two-party circuit -- each party's own
// parsed DNF (see utils/dimacs_dnf.h) in, a single revealed raw Karp-Luby
// numerator out. "Raw" means un-normalized: see
// gadgets/karp_luby/karp_luby_estimate.h for what the caller still has to
// do in plaintext (divide by K * gadgets::lookup_scale<CUBES*CUBES>()) to
// get the actual estimate.
//
// This is the pipeline steps 1-7 documented in src/main.cpp's file
// comment, lifted into a shared function so src/main.cpp (the interactive
// demo, small fixed K) and src/bench/bench_karp_luby.cpp (the benchmark,
// K chosen from a target epsilon via
// gadgets/karp_luby/karp_luby_estimate.h's karp_luby_trials()) don't each
// carry their own copy of it.
//
// Not Ctx-generic like the gadgets/ headers -- this is SH2PCSession-only
// (it does real network I/O via sess.input/sess.reveal), unlike the pure
// wire-level gadgets it calls, which stay testable under ClearSession.

#pragma once

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
#include <cstdint>

using emp::SH2PCSession;
using emp::BitVec_T;
using emp::Bit_T;
using emp::PRG;
using emp::ALICE;
using emp::BOB;
using emp::PUBLIC;

using Ctx = SH2PCSession::ctx_t;

template <int VARS, int CUBES, int K>
uint64_t run_karp_luby_pipeline(SH2PCSession& sess, const dimacs_dnf::Dnf<VARS, CUBES>& my_dnf) {
    using namespace gadgets;
    using BV = BitVec_T<Ctx, VARS>;
    constexpr int PRODUCT = CUBES * CUBES;

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
        // assignment (gadgets/karp_luby/random_assignment.h): a second,
        // independent joint random bitstring (same free-XOR construction,
        // drawn/fed the same way) fills in whatever the cube leaves
        // unconstrained.
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

    return sess.reveal(estimate, PUBLIC).value();
}
