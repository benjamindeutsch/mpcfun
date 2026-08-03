// run_vazirani_pipeline: the real two-party circuit -- each party's own
// parsed DNF (see utils/dimacs_dnf.h) in, a single revealed raw Vazirani
// numerator out. "Raw" means un-normalized: see
// gadgets/vazirani/vazirani_estimate.h for what the caller still has to
// do in plaintext (divide by K * gadgets::lookup_scale<CUBES*CUBES>()) to
// get the actual estimate.
//
// This is the pipeline steps 1-7 documented in src/main.cpp's file
// comment, lifted into a shared function so src/main.cpp (the interactive
// demo, small fixed K) and src/bench/bench_vazirani.cpp (the benchmark,
// K chosen from a target epsilon via
// gadgets/vazirani/vazirani_estimate.h's vazirani_trials()) don't each
// carry their own copy of it.
//
// Not Ctx-generic like the gadgets/ headers -- this is SH2PCSession-only
// (it does real network I/O via sess.input/sess.reveal), unlike the pure
// wire-level gadgets it calls, which stay testable under ClearSession.
//
// Returns an unsigned __int128, not a uint64_t: emp::UInt_T<Ctx,N>'s
// clear_t is a plain uint64_t (a hard emp-toolkit ceiling -- .reveal()/
// .input()/.constant(uint64_t) on a UInt_T literally cannot round-trip
// more than 64 bits), but VaziraniEstimate<Ctx,VARS,PRODUCT,K>'s width is
// (VARS + bits_for(PRODUCT)) + (kDivideLookupScaleBits+1) + bits_for(K) --
// comfortably under 64 bits at VARS=16 (53 bits), but over it by VARS=32
// (73 bits, at K=7993 -- see gadgets/vazirani/vazirani_estimate.h's
// vazirani_trials() and src/bench/bench_vazirani.cpp for where that K
// comes from). See reveal_wide() below for how the final reveal handles
// that -- it's width-general (recurses on however far over 64 bits a
// value is), not hardcoded to any one sweep size.

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
#include "gadgets/vazirani/count_satisfied_cubes.h"
#include "gadgets/vazirani/divide_lookup.h"
#include "gadgets/vazirani/vazirani_estimate.h"

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

// PipelineBreakdown: per-gadget network bytes, for callers that want to
// see which gadget dominates bandwidth (see src/bench/bench_vazirani.cpp)
// rather than just the pipeline's total. Not used by src/main.cpp, which
// never constructs one -- run_vazirani_pipeline's breakdown/io parameters
// default to nullptr, so that call site pays nothing for this.
struct PipelineBreakdown {
    struct Phase {
        std::string name;
        uint64_t sent = 0;
        uint64_t recv = 0;
    };
    std::vector<Phase> phases;

    // Accumulates into the named phase (creating it on first use) --
    // per-trial gadgets (select_cube, random_assignment, ...) call this
    // once per Vazirani trial, so the same name's bytes sum across all K.
    void add(const char* name, uint64_t sent, uint64_t recv) {
        for (auto& p : phases) {
            if (p.name == name) {
                p.sent += sent;
                p.recv += recv;
                return;
            }
        }
        phases.push_back(Phase{name, sent, recv});
    }
};

// measure(io, breakdown, name, fn): runs fn() and returns its result;
// if breakdown is non-null, also records the NetIO bytes fn() sent/
// received under `name` (io must be non-null whenever breakdown is).
// A no-op wrapper (just fn()) when breakdown is null.
template <class Fn>
auto measure(NetIO* io, PipelineBreakdown* breakdown, const char* name, Fn&& fn) -> decltype(fn()) {
    if (!breakdown) return fn();
    uint64_t sent0 = io->send_counter, recv0 = io->recv_counter;
    auto result = fn();
    breakdown->add(name, io->send_counter - sent0, io->recv_counter - recv0);
    return result;
}

// PipelineMemoryReport: each pipeline structure's exact byte size
// (sizeof(type) * count), computed statically -- not measured at runtime.
// Runtime process memory (e.g. getrusage().ru_maxrss) wouldn't give a
// meaningful per-gadget breakdown here: most of what a gadget allocates is
// stack space freed the moment its function returns, and RSS is a
// cumulative high-water mark that doesn't cleanly reset between phases.
// The sizes below are exactly what each structure occupies while it's
// alive, known from its type alone.
struct PipelineMemoryReport {
    struct Entry {
        std::string name;
        uint64_t bytes;
    };
    std::vector<Entry> entries;

    void add(const char* name, uint64_t bytes) { entries.push_back(Entry{name, bytes}); }
};

template <int VARS, int CUBES, int K>
PipelineMemoryReport pipeline_memory_report() {
    using namespace gadgets;
    constexpr int PRODUCT = CUBES * CUBES;
    using TotalWeight = DnfWeight<Ctx, VARS, PRODUCT>;

    PipelineMemoryReport r;
    r.add("alice_cubes + bob_cubes", 2ull * (uint64_t)CUBES * sizeof(CircuitCube<Ctx, VARS>));
    r.add("conjunction (conjoin_dnf's output)", (uint64_t)PRODUCT * sizeof(CircuitCube<Ctx, VARS>));
    r.add("weights (cube_weights' output)", (uint64_t)PRODUCT * sizeof(CubeWeight<Ctx, VARS>));
    r.add("intervals (cube_intervals' output)", (uint64_t)(PRODUCT + 1) * sizeof(TotalWeight));
    r.add("reciprocals (K divide_lookup outputs)", (uint64_t)K * sizeof(DivideLookupResult<Ctx, PRODUCT>));
    r.add("one trial's live locals (selected/assignment/satisfied_count)",
          sizeof(CubeData<Ctx, VARS>) + 2 * sizeof(BitVec_T<Ctx, VARS>) + sizeof(SatisfiedCount<Ctx, PRODUCT>));
    return r;
}

// reveal_wide<W>(sess, value, PUBLIC): like sess.reveal(value,
// PUBLIC).value(), but works for W > 64 -- emp::UInt_T<Ctx,W>'s clear_t is
// uint64_t, so .reveal() only round-trips W <= 64 directly (see this
// file's top comment). Slices value into a low 64-bit chunk and a high
// (W-64)-bit chunk (UInt_T::slice<Lo,Hi>, pure wiring -- no extra gates),
// reveals each separately (recursing again if the high chunk is itself
// still over 64 bits), and reassembles them into an unsigned __int128 in
// plaintext -- the recursion itself handles any W, but the __int128
// accumulator caps the usable range at 128 bits total, comfortably above
// what this pipeline's sweep needs today (73 bits at VARS=32 -- see this
// file's top comment) but worth knowing if a future sweep size needs more.
// Scoped to PUBLIC reveals only (every caller in this file only ever
// needs that); ALICE/BOB/XOR aren't handled.
template <int W>
unsigned __int128 reveal_wide(SH2PCSession& sess, const emp::UInt_T<Ctx, W>& value) {
    if constexpr (W <= 64) {
        return (unsigned __int128)sess.reveal(value, PUBLIC).value();
    } else {
        unsigned __int128 lo = reveal_wide<64>(sess, value.template slice<0, 64>());
        unsigned __int128 hi = reveal_wide<W - 64>(sess, value.template slice<64, W>());
        return lo | (hi << 64);
    }
}

// io/breakdown: optional, both default nullptr -- pass both together (from
// src/bench/bench_vazirani.cpp) to get a per-gadget network breakdown (see
// PipelineBreakdown above); src/main.cpp passes neither, so every
// measure() call below is a plain, zero-overhead direct call there.
template <int VARS, int CUBES, int K>
unsigned __int128 run_vazirani_pipeline(SH2PCSession& sess, const dimacs_dnf::Dnf<VARS, CUBES>& my_dnf,
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

    // K independent Vazirani trials. Each trial draws its own fresh joint
    // randomness (both for select_cube's sampling step and for
    // random_assignment's free-variable fill), so the K samples are
    // independent -- required for the estimator's variance to actually
    // shrink with K. Nothing about any trial is revealed -- only the final
    // combined estimate is, below. Per-trial breakdown entries accumulate
    // across all K trials (see PipelineBreakdown::add above), so e.g.
    // "sample_in_range" ends up as that gadget's total over the whole run.
    using RandBits = SampleBits<Ctx, VARS, PRODUCT>;
    array<DivideLookupResult<Ctx, PRODUCT>, K> reciprocals{};

    for (int t = 0; t < K; ++t) {
        uint64_t trial_input_sent0 = breakdown ? io->send_counter : 0;
        uint64_t trial_input_recv0 = breakdown ? io->recv_counter : 0;
        array<bool, (size_t)TotalWidth> my_random_bits{};
        PRG().random_bool(my_random_bits.data(), TotalWidth);
        RandBits alice_r = sess.input<RandBits>(ALICE, my_random_bits);
        RandBits bob_r   = sess.input<RandBits>(BOB,   my_random_bits);
        if (breakdown) breakdown->add("trial_random_input_feeding", io->send_counter - trial_input_sent0, io->recv_counter - trial_input_recv0);

        // select_cube's three pieces (gadgets/general/select_cube.h),
        // called directly instead of through the composed select_cube()
        // so each gets its own breakdown entry rather than being lumped
        // into one "select_cube" bucket.
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

        SatisfiedCount<Ctx, PRODUCT> satisfied_count = measure(io, breakdown, "count_satisfied_cubes", [&] {
            return count_satisfied_cubes<Ctx, VARS, PRODUCT>(assignment, conjunction);
        });
        reciprocals[(size_t)t] = measure(io, breakdown, "divide_lookup", [&] {
            return divide_lookup<Ctx, PRODUCT>(satisfied_count);
        });
    }

    VaziraniEstimate<Ctx, VARS, PRODUCT, K> estimate = measure(io, breakdown, "vazirani_estimate", [&] {
        return vazirani_estimate<Ctx, VARS, PRODUCT, K>(total, reciprocals);
    });

    return measure(io, breakdown, "reveal", [&] {
        return reveal_wide<VaziraniEstimate<Ctx, VARS, PRODUCT, K>::width()>(sess, estimate);
    });
}
