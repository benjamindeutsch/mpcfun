// Algorithm-agnostic pipeline instrumentation, shared by every SH2PCSession
// pipeline in this project (see pipeline/vazirani_pipeline.h,
// pipeline/karp_luby_pipeline.h): per-gadget network accounting
// (PipelineBreakdown/measure()), a per-structure static memory report shape
// (PipelineMemoryReport -- each pipeline populates its own via its own
// pipeline_memory_report<VARS,CUBES,K>(), since the structures differ per
// algorithm), and a >64-bit-safe reveal (reveal_wide<W>()). None of this
// code is specific to any one estimator -- it was originally embedded in
// vazirani_pipeline.h and pulled out here once a second pipeline
// (Karp-Luby) needed the exact same plumbing.

#pragma once

#include "emp-sh2pc/emp-sh2pc.h"

#include <cstdint>
#include <string>
#include <vector>

using emp::SH2PCSession;
using emp::NetIO;
using emp::PUBLIC;

// PipelineBreakdown: per-gadget network bytes, for callers that want to see
// which gadget dominates bandwidth rather than just the pipeline's total.
struct PipelineBreakdown {
    struct Phase {
        std::string name;
        uint64_t sent = 0;
        uint64_t recv = 0;
    };
    std::vector<Phase> phases;

    // Accumulates into the named phase (creating it on first use) --
    // per-trial gadgets call this once per trial, so the same name's bytes
    // sum across all K.
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

// measure(io, breakdown, name, fn): runs fn() and returns its result; if
// breakdown is non-null, also records the NetIO bytes fn() sent/received
// under `name` (io must be non-null whenever breakdown is). A no-op wrapper
// (just fn()) when breakdown is null.
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
// cumulative high-water mark that doesn't cleanly reset between phases. The
// sizes below are exactly what each structure occupies while it's alive,
// known from its type alone. Each pipeline populates its own instance (its
// entries reflect that pipeline's own locals) -- see e.g.
// vazirani_pipeline.h's/karp_luby_pipeline.h's own pipeline_memory_report().
struct PipelineMemoryReport {
    struct Entry {
        std::string name;
        uint64_t bytes;
    };
    std::vector<Entry> entries;

    void add(const char* name, uint64_t bytes) { entries.push_back(Entry{name, bytes}); }
};

// reveal_wide<W>(sess, value): like sess.reveal(value, PUBLIC).value(), but
// works for W > 64 -- emp::UInt_T<Ctx,W>'s clear_t is uint64_t, so
// .reveal() only round-trips W <= 64 directly. Slices value into a low
// 64-bit chunk and a high (W-64)-bit chunk (UInt_T::slice<Lo,Hi>, pure
// wiring -- no extra gates), reveals each separately (recursing again if
// the high chunk is itself still over 64 bits), and reassembles them into
// an unsigned __int128 in plaintext -- the recursion itself handles any W,
// but the __int128 accumulator caps the usable range at 128 bits total.
// Scoped to PUBLIC reveals only (every caller in this project only ever
// needs that); ALICE/BOB/XOR aren't handled.
template <int W>
unsigned __int128 reveal_wide(SH2PCSession& sess, const emp::UInt_T<SH2PCSession::ctx_t, W>& value) {
    if constexpr (W <= 64) {
        return (unsigned __int128)sess.reveal(value, PUBLIC).value();
    } else {
        unsigned __int128 lo = reveal_wide<64>(sess, value.template slice<0, 64>());
        unsigned __int128 hi = reveal_wide<W - 64>(sess, value.template slice<64, W>());
        return lo | (hi << 64);
    }
}
