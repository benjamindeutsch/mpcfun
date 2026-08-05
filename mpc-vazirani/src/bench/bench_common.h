// Algorithm-agnostic benchmark plumbing, shared by every bench_*.cpp in
// this project (see bench/bench_vazirani.cpp, bench/bench_compare.cpp):
// byte/duration formatting, the breakdown printer, the RLIMIT_STACK bump
// emp-toolkit's large-K sweeps need, and a generic per-size sweep-point
// runner (run_one<Adapter,VARS>()) that any pipeline can plug into by
// implementing the small Adapter interface documented below -- originally
// this was all embedded directly in bench_vazirani.cpp and hardcoded to
// run_vazirani_pipeline, pulled out and generalized once a second pipeline
// (Karp-Luby) needed the exact same plumbing.

#pragma once

#include "emp-sh2pc/emp-sh2pc.h"
#include "bench/ground_truth.h"
#include "pipeline/instrumentation.h"
#include "utils/dimacs_dnf.h"
#include "utils/random_dnf.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

namespace bench {

// Binary units (KiB/MiB/GiB, base 1024), matching emp-tool's own
// NetIO::get_statistics_string() -- the same convention already showing up
// right after a bench binary's own output (its "Network statistics:"
// end-of-process printout), so bytes aren't reported two different ways in
// the same console dump.
inline std::string format_bytes(uint64_t bytes) {
    static const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double v = (double)bytes;
    int i = 0;
    while (v >= 1024.0 && i < 4) {
        v /= 1024.0;
        ++i;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f %s", v, units[i]);
    return std::string(buf);
}

// us -> the largest whole unit that keeps it readable (us/ms/s/min/h) --
// std::ostream's default double formatting switches to scientific notation
// past ~1e6 (e.g. "1.07004e+06 ms" for a multi-minute elapsed time), which
// is exactly the un-readable case this avoids.
inline std::string format_duration(double us) {
    if (us < 1000.0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f us", us);
        return std::string(buf);
    }
    double ms = us / 1000.0;
    if (ms < 1000.0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f ms", ms);
        return std::string(buf);
    }
    double s = ms / 1000.0;
    if (s < 60.0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f s", s);
        return std::string(buf);
    }
    double min = s / 60.0;
    if (min < 60.0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f min", min);
        return std::string(buf);
    }
    double h = min / 60.0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f h", h);
    return std::string(buf);
}

// Prints breakdown's phases (network bytes) and mem's structures (static
// sizes), each sorted biggest-first, so "which gadget dominates" is
// readable at a glance rather than requiring the reader to scan/sum
// themselves.
inline void print_breakdown(const std::string& who, const PipelineBreakdown& breakdown, const PipelineMemoryReport& mem) {
    std::vector<PipelineBreakdown::Phase> phases = breakdown.phases;
    std::sort(phases.begin(), phases.end(), [](const auto& a, const auto& b) {
        return (a.sent + a.recv) > (b.sent + b.recv);
    });
    for (const auto& p : phases)
        std::cout << who << "     [net]  " << p.name << ": sent=" << format_bytes(p.sent)
                  << " recv=" << format_bytes(p.recv) << " total=" << format_bytes(p.sent + p.recv) << std::endl;

    std::vector<PipelineMemoryReport::Entry> entries = mem.entries;
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.bytes > b.bytes; });
    for (const auto& e : entries)
        std::cout << who << "     [mem]  " << e.name << ": " << format_bytes(e.bytes) << std::endl;
}

// Raises the soft RLIMIT_STACK up front as cheap headroom for large-K/M
// sweep points (Linux grows the main thread's stack on demand up to
// whatever the limit is *at fault time*, so doing this after the process
// has already started still works). Every bench binary's main() should
// call this before running its sweep.
inline void raise_stack_limit(rlim_t bytes = 256ull * 1024 * 1024) {
    rlimit rl{};
    getrlimit(RLIMIT_STACK, &rl);
    rl.rlim_cur = std::min(rl.rlim_max, bytes);
    setrlimit(RLIMIT_STACK, &rl);
}

// An Adapter wraps one pipeline (see pipeline/vazirani_pipeline.h's
// VaziraniAdapter, pipeline/karp_luby_pipeline.h's KarpLubyAdapter) for the
// generic sweep-point runner below. Required members:
//
//   static constexpr const char* kName;
//   template <int VARS,int CUBES,int K>
//   static unsigned __int128 run(emp::SH2PCSession&, const dimacs_dnf::Dnf<VARS,CUBES>&,
//                                 emp::NetIO*, PipelineBreakdown*);
//   static constexpr int trials(int cubes, double epsilon, double delta);
//   template <int VARS,int CUBES,int K>
//   static double unscale(unsigned __int128 raw);
//   template <int VARS,int CUBES,int K>
//   static PipelineMemoryReport memory_report();
//
// Adding a new pipeline to the benchmark sweep means writing one of these,
// not touching run_one() itself.
//
// Epsilon/Delta are template parameters (a C++20 floating-point non-type
// template parameter), not runtime arguments: K has to be a compile-time
// constant (every K-templated gadget/pipeline call below needs it as
// such), and Adapter::trials() is constexpr, so computing K from a
// function parameter wouldn't itself be a constant expression -- passing
// Epsilon/Delta in at the template level keeps `constexpr int K` valid.
//
// `party`/`max_overlap`/`seed` replace what used to be a DNF file path:
// both processes call utils/random_dnf.h's generate_random_dnf_pair with
// the same (public) VARS/CUBES/max_overlap/seed, so they independently
// reproduce the identical pair without exchanging anything, then each
// keeps only its own half (see that file's own comment on why this is
// fine for a synthetic benchmark but wouldn't be for real private input).
// Sized fresh to every sweep point instead of reusing/padding one small
// fixed file, so max_overlap stays meaningful (as a literal count, capped
// at VARS) at every point in the sweep, not just the smallest.
template <class Adapter, int VARS, int CUBES, double Epsilon, double Delta>
void run_one(emp::SH2PCSession& sess, emp::NetIO* io, int party, int max_overlap, uint64_t seed,
             const std::string& who) {
    // CUBES, not VARS: an Adapter's trials(cubes, ...) bounds K by the
    // number of cubes in the conjoined DNF -- a separate parameter from
    // VARS (the pipelines/gadgets underneath already treat them
    // independently; callers are free to sweep VARS and CUBES separately,
    // not just along VARS=CUBES).
    constexpr int K = Adapter::trials(CUBES, Epsilon, Delta);

    auto pair = random_dnf::generate_random_dnf_pair<VARS, CUBES>(max_overlap, seed);
    const dimacs_dnf::Dnf<VARS, CUBES>& my_dnf = (party == emp::ALICE) ? pair.alice : pair.bob;

    // Only Alice's process shells out to pepin -- both processes generated
    // the identical pair above, so either could, but there's no reason to
    // pay for it twice (see bench/ground_truth.h). Done before io->sync()
    // below so pepin's own runtime (up to ~1s at this sweep's largest
    // point) never counts against the timed protocol region.
    std::optional<ground_truth::PepinResult> truth;
    if (party == emp::ALICE) truth = ground_truth::run_pepin_sanity_check<VARS, CUBES>(pair.alice, pair.bob);

    // Only the protocol call itself is timed/counted -- not the DNF
    // generation above (local, pre-network data prep) -- matching
    // emp-tool's own bench convention of not billing unrelated setup work
    // to the timed call. io->sync() (a 1-byte ping/pong) lines both
    // parties up before the clock starts, so one party can't race ahead
    // between sweep points and skew the next config's timing.
    io->sync();
    uint64_t sent0 = io->send_counter, recv0 = io->recv_counter, rounds0 = io->rounds;
    auto start = emp::clock_start();
    // unsigned __int128, not uint64_t: emp::UInt_T's reveal ceiling is 64
    // bits -- see pipeline/instrumentation.h's reveal_wide().
    PipelineBreakdown breakdown;
    unsigned __int128 estimate_raw = Adapter::template run<VARS, CUBES, K>(sess, my_dnf, io, &breakdown);
    double elapsed_us = emp::time_from(start);
    uint64_t sent = io->send_counter - sent0;
    uint64_t recv = io->recv_counter - recv0;
    uint64_t rounds = io->rounds - rounds0;

    double estimate = Adapter::template unscale<VARS, CUBES, K>(estimate_raw);

    std::cout << who << " [" << Adapter::kName << "] VARS=" << VARS << " CUBES=" << CUBES
              << "  epsilon=" << Epsilon << " delta=" << Delta << " K=" << K
              << "  max_overlap=" << max_overlap
              << "  estimate=" << estimate;
    if (truth)
        std::cout << "  pepin_ground_truth=" << truth->estimate
                   << "  pepin_elapsed=" << format_duration(truth->elapsed_us);
    std::cout << "  elapsed=" << format_duration(elapsed_us)
              << "  sent=" << format_bytes(sent) << " recv=" << format_bytes(recv) << " rounds=" << rounds
              << std::endl;

    print_breakdown(who, breakdown, Adapter::template memory_report<VARS, CUBES, K>());
}

}  // namespace bench
