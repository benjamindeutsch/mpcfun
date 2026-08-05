// run_karp_luby_pipeline: the Karp-Luby sibling of
// pipeline/vazirani_pipeline.h's run_vazirani_pipeline -- each party's own
// parsed DNF (see utils/dimacs_dnf.h) in, a single revealed raw Karp-Luby
// numerator out. "Raw" means un-normalized: see
// gadgets/karp_luby/karp_luby_estimate.h for what the caller still has to
// do in plaintext (divide by K) to get the actual estimate.
//
// Identical to run_vazirani_pipeline through "sample a cube weighted by
// its model count, then sample a satisfying assignment of it" (every
// gadgets/general/* call below matches run_vazirani_pipeline verbatim).
// The two pipelines diverge only in what each trial does with the sampled
// assignment: Vazirani counts how many conjunction cubes it satisfies and
// looks up 1/count (gadgets/vazirani/count_satisfied_cubes.h +
// gadgets/vazirani/divide_lookup.h); Karp-Luby instead checks whether the
// sampled cube is the *canonical* (lowest-indexed) cube covering that
// assignment (gadgets/karp_luby/is_canonical_sample.h) and accumulates a
// {0,1} indicator per trial instead of a reciprocal.
//
// Not Ctx-generic like the gadgets/ headers -- this is SH2PCSession-only
// (it does real network I/O via sess.input/sess.reveal), unlike the pure
// wire-level gadgets it calls, which stay testable under ClearSession.

#pragma once

#include "emp-sh2pc/emp-sh2pc.h"
#include "utils/dimacs_dnf.h"
#include "gadgets/circuit_cube.h"
#include "gadgets/general/dnf_distribute.h"
#include "gadgets/general/cube_weight.h"
#include "gadgets/general/dnf_weight.h"
#include "gadgets/general/cube_intervals.h"
#include "gadgets/general/select_cube.h"
#include "gadgets/general/random_assignment.h"
#include "gadgets/karp_luby/is_canonical_sample.h"
#include "gadgets/karp_luby/karp_luby_estimate.h"
#include "pipeline/instrumentation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using emp::SH2PCSession;
using emp::NetIO;
using emp::BitVec_T;
using emp::Bit_T;
using emp::PRG;
using emp::ALICE;
using emp::BOB;
using emp::PUBLIC;

using Ctx = SH2PCSession::ctx_t;

// This pipeline's own memory report -- entries reflect its own locals
// (`indicators`, a K-sized array<Bit_T,K>, in place of
// vazirani_pipeline.h's much larger `reciprocals`; see that file's own
// vazirani_pipeline_memory_report for the shared shape this mirrors).
// Named per-algorithm (not just "pipeline_memory_report") since
// bench_compare.cpp includes both pipeline headers in one translation
// unit, so a shared name would collide with vazirani_pipeline.h's.
template <int VARS, int CUBES, int K>
PipelineMemoryReport karp_luby_pipeline_memory_report() {
    using namespace gadgets;
    constexpr int PRODUCT = CUBES * CUBES;
    using TotalWeight = DnfWeight<Ctx, VARS, PRODUCT>;

    PipelineMemoryReport r;
    r.add("alice_cubes + bob_cubes", 2ull * (uint64_t)CUBES * sizeof(CircuitCube<Ctx, VARS>));
    r.add("conjunction (conjoin_dnf's output)", (uint64_t)PRODUCT * sizeof(CircuitCube<Ctx, VARS>));
    r.add("weights (cube_weights' output)", (uint64_t)PRODUCT * sizeof(CubeWeight<Ctx, VARS>));
    r.add("intervals (cube_intervals' output)", (uint64_t)(PRODUCT + 1) * sizeof(TotalWeight));
    r.add("indicators (K is_canonical_sample outputs)", (uint64_t)K * sizeof(Bit_T<Ctx>));
    r.add("one trial's live locals (selected/assignment/canonical)",
          sizeof(CubeData<Ctx, VARS>) + 2 * sizeof(BitVec_T<Ctx, VARS>) + sizeof(Bit_T<Ctx>));
    return r;
}

// io/breakdown: optional, both default nullptr -- pass both together (from
// src/bench/bench_vazirani.cpp or bench_compare.cpp) to get a per-gadget
// network breakdown (see pipeline/instrumentation.h's PipelineBreakdown);
// no caller currently passes neither (unlike vazirani_pipeline.h's
// src/main.cpp demo), but the default keeps this pipeline usable the same
// way if a Karp-Luby demo is added later.
template <int VARS, int CUBES, int K>
unsigned __int128 run_karp_luby_pipeline(SH2PCSession& sess, const dimacs_dnf::Dnf<VARS, CUBES>& my_dnf,
                                          NetIO* io = nullptr, PipelineBreakdown* breakdown = nullptr) {
    using namespace gadgets;
    using BV = BitVec_T<Ctx, VARS>;
    constexpr int PRODUCT = CUBES * CUBES;

    // Feed each party's own parsed cubes in as that party's private input.
    // Both parties run both loops -- as with any secret input in this
    // toolkit, only the named owner's argument is actually used (see
    // SH2PCSession::input), so each process just passes its own local
    // my_dnf data regardless of which owner it's calling for.
    uint64_t cube_input_sent0 = breakdown ? io->send_counter : 0;
    uint64_t cube_input_recv0 = breakdown ? io->recv_counter : 0;
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
    if (breakdown) breakdown->add("cube_input_feeding", io->send_counter - cube_input_sent0, io->recv_counter - cube_input_recv0);

    array<CircuitCube<Ctx, VARS>, PRODUCT> conjunction =
        measure(io, breakdown, "conjoin_dnf", [&] { return conjoin_dnf<Ctx, VARS, CUBES>(alice_cubes, bob_cubes); });
    array<CubeWeight<Ctx, VARS>, PRODUCT> weights =
        measure(io, breakdown, "cube_weights", [&] { return cube_weights<Ctx, VARS, PRODUCT>(conjunction); });

    using TotalWeight = DnfWeight<Ctx, VARS, PRODUCT>;
    constexpr int TotalWidth = TotalWeight::width();

    TotalWeight total = measure(io, breakdown, "dnf_weight", [&] { return dnf_weight<Ctx, VARS, PRODUCT>(weights); });
    array<TotalWeight, PRODUCT + 1> intervals =
        measure(io, breakdown, "cube_intervals", [&] { return cube_intervals<Ctx, VARS, PRODUCT>(weights); });

    // K independent Karp-Luby trials. Each trial draws its own fresh joint
    // randomness (both for select_cube's sampling step and for
    // random_assignment's free-variable fill), so the K samples are
    // independent -- required for the estimator's variance to actually
    // shrink with K. Nothing about any trial is revealed -- only the final
    // combined estimate is, below. Per-trial breakdown entries accumulate
    // across all K trials (see PipelineBreakdown::add), so e.g.
    // "sample_in_range" ends up as that gadget's total over the whole run.
    // This loop is identical to run_vazirani_pipeline's through
    // random_assignment -- see that file for the per-step comments.
    using RandBits = SampleBits<Ctx, VARS, PRODUCT>;
    array<Bit_T<Ctx>, K> indicators{};

    for (int t = 0; t < K; ++t) {
        uint64_t trial_input_sent0 = breakdown ? io->send_counter : 0;
        uint64_t trial_input_recv0 = breakdown ? io->recv_counter : 0;
        array<bool, (size_t)TotalWidth> my_random_bits{};
        PRG().random_bool(my_random_bits.data(), TotalWidth);
        RandBits alice_r = sess.input<RandBits>(ALICE, my_random_bits);
        RandBits bob_r   = sess.input<RandBits>(BOB,   my_random_bits);
        if (breakdown) breakdown->add("trial_random_input_feeding", io->send_counter - trial_input_sent0, io->recv_counter - trial_input_recv0);

        // select_cube's three pieces (gadgets/general/select_cube.h),
        // called directly instead of through the composed select_cube() so
        // each gets its own breakdown entry rather than being lumped into
        // one "select_cube" bucket. Unlike run_vazirani_pipeline, `index`
        // itself is kept around (not just `selected`): is_canonical_sample
        // below needs to compare other cubes' indices against it.
        TotalWeight z = measure(io, breakdown, "sample_in_range", [&] {
            return sample_in_range<Ctx, VARS, PRODUCT>(alice_r, bob_r, total);
        });
        CubeIndex<Ctx, PRODUCT> index = measure(io, breakdown, "select_cube_index", [&] {
            return select_cube_index<Ctx, VARS, PRODUCT>(z, intervals);
        });
        CubeData<Ctx, VARS> selected = measure(io, breakdown, "cube_at_index", [&] {
            return cube_at_index<Ctx, VARS, PRODUCT>(index, conjunction);
        });

        // Extend the selected cube into a full random satisfying
        // assignment (gadgets/general/random_assignment.h): a second,
        // independent joint random bitstring (same free-XOR construction,
        // drawn/fed the same way) fills in whatever the cube leaves
        // unconstrained.
        uint64_t assign_input_sent0 = breakdown ? io->send_counter : 0;
        uint64_t assign_input_recv0 = breakdown ? io->recv_counter : 0;
        array<bool, VARS> my_assignment_bits{};
        PRG().random_bool(my_assignment_bits.data(), VARS);
        BV assignment_alice_r = sess.input<BV>(ALICE, my_assignment_bits);
        BV assignment_bob_r   = sess.input<BV>(BOB,   my_assignment_bits);
        if (breakdown) breakdown->add("trial_random_input_feeding", io->send_counter - assign_input_sent0, io->recv_counter - assign_input_recv0);

        BV assignment = measure(io, breakdown, "random_assignment", [&] {
            return random_assignment<Ctx, VARS>(selected, assignment_alice_r, assignment_bob_r);
        });

        // Karp-Luby's divergence point (see
        // gadgets/karp_luby/is_canonical_sample.h): is the sampled cube the
        // lowest-indexed cube this assignment satisfies?
        indicators[(size_t)t] = measure(io, breakdown, "is_canonical_sample", [&] {
            return is_canonical_sample<Ctx, VARS, PRODUCT>(index, assignment, conjunction);
        });
    }

    KarpLubyEstimate<Ctx, VARS, PRODUCT, K> estimate = measure(io, breakdown, "karp_luby_estimate", [&] {
        return karp_luby_estimate<Ctx, VARS, PRODUCT, K>(total, indicators);
    });

    return measure(io, breakdown, "reveal", [&] {
        return reveal_wide<KarpLubyEstimate<Ctx, VARS, PRODUCT, K>::width()>(sess, estimate);
    });
}

// KarpLubyAdapter: wraps this pipeline for bench/bench_common.h's generic
// run_one<Adapter,VARS>() sweep runner (see that file's Adapter interface
// comment) -- lets bench_compare.cpp drive this pipeline through the same
// generic harness pipeline/vazirani_pipeline.h's VaziraniAdapter uses.
struct KarpLubyAdapter {
    static constexpr const char* kName = "karp_luby";

    template <int VARS, int CUBES, int K>
    static unsigned __int128 run(SH2PCSession& sess, const dimacs_dnf::Dnf<VARS, CUBES>& dnf,
                                  NetIO* io, PipelineBreakdown* breakdown) {
        return run_karp_luby_pipeline<VARS, CUBES, K>(sess, dnf, io, breakdown);
    }

    static constexpr int trials(int cubes, double epsilon, double delta) {
        return gadgets::karp_luby_trials(cubes, epsilon, delta);
    }

    // No fixed-point scale to undo here (unlike VaziraniAdapter's
    // lookup_scale term) -- see gadgets/karp_luby/karp_luby_estimate.h.
    template <int VARS, int CUBES, int K>
    static double unscale(unsigned __int128 raw) {
        return (double)raw / (double)K;
    }

    template <int VARS, int CUBES, int K>
    static PipelineMemoryReport memory_report() {
        return karp_luby_pipeline_memory_report<VARS, CUBES, K>();
    }
};
