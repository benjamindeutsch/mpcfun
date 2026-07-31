// bench_karp_luby: times the real two-party Karp-Luby circuit
// (pipeline/karp_luby_pipeline.h) across a sweep of (VARS,CUBES) sizes
// (VARS=CUBES, doubling from 4 to 16), each at a *fixed*, meaningful
// epsilon/delta -- ApproxMC's own defaults, epsilon=0.8 (tolerance) and
// delta=0.2 (failure probability, i.e. success probability >= 1-delta =
// 0.8) -- rather than the loosened-per-size epsilon an earlier version of
// this file used just to keep the sweep fast.
//
// Why the sweep stops at VARS=16, not 64: K = ceil((1/delta)*
// (VARS^2-1)^2/epsilon^2) (gadgets/karp_luby/karp_luby_estimate.h's
// karp_luby_trials()) grows as O(VARS^4). At epsilon=0.8, delta=0.2, that's:
//
//   VARS   M=CUBES^2   K            K*M (work)
//   4      16          1758         28128
//   8      64          31008        1984512
//   16     256         508008       130050048
//   32     1024        8176008      8372744192      (~26 hours, extrapolated)
//   64     4096        131007891    536608092672     (~110 days, extrapolated)
//
// (extrapolated from this sweep's own measured work-vs-time relationship
// at the smaller sizes -- see "Benchmarks" in README.md). VARS=32/64
// aren't reachable with meaningful accuracy on this per-trial-gate
// architecture; keeping them in the sweep would mean either running this
// benchmark for months, or silently going back to the meaningless
// loosened-epsilon regime this change was specifically meant to replace.
//
// It follows emp-toolkit's own benchmark conventions (see
// emp-tool/docs/benchmark_conventions.md): the timed region is wrapped in
// clock_start()/time_from() (emp-tool/runtime/core/utils.h), isolated per
// config from unrelated setup, and per-config network bytes/rounds are
// captured via the same before/after
// io->send_counter/io->recv_counter/io->rounds snapshot
// emp-ot/bench/bench.h's time_ot() uses -- NetIO's own end-of-process
// printout only gives a session-wide total, since one connection is
// reused across every config here.
//
// Usage:
//   ./bench_karp_luby <party: 1=ALICE, 2=BOB> <path-to-dimacs-dnf-file>
//
// Same two-party launch convention as sh2pc_demo: Alice listens on a fixed
// localhost port (default 12345, override with EMP_PORT), Bob connects
// (override the address with EMP_PEER_IP). The same small DNF file is
// reused at every size in the sweep -- dimacs_dnf::parse<VARS,CUBES> only
// requires the file's declared vars/cubes fit within the VARS/CUBES
// capacity, padding the rest (see utils/dimacs_dnf.h); circuit cost is the
// same regardless of how much of that capacity is "real" vs. padding, by
// design (that's the whole point of padding), so this is a valid way to
// benchmark performance at a given size without a separately crafted DNF
// file per size.
//
// Expect the VARS=16 point alone to take on the order of tens of minutes
// (K=508008, each trial scanning M=256 cubes) -- run this in the
// background rather than waiting on it interactively.

#include "pipeline/karp_luby_pipeline.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

using namespace emp;
using namespace gadgets;

// ApproxMC's own defaults: tolerance epsilon=0.8, failure probability
// delta=0.2 (success probability >= 0.8) -- see this file's top comment.
constexpr double EPSILON = 0.8;
constexpr double DELTA = 0.2;

// Binary units (KiB/MiB/GiB, base 1024), matching emp-tool's own
// NetIO::get_statistics_string() -- the same convention already showing up
// right after this program's own output (its "Network statistics:"
// end-of-process printout), so bytes aren't reported two different ways in
// the same console dump.
std::string format_bytes(uint64_t bytes) {
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

// Prints breakdown's phases (network bytes) and mem's structures (static
// sizes), each sorted biggest-first, so "which gadget dominates" is
// readable at a glance rather than requiring the reader to scan/sum
// themselves.
void print_breakdown(const std::string& who, const PipelineBreakdown& breakdown, const PipelineMemoryReport& mem) {
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

template <int VARS>
void run_one(SH2PCSession& sess, NetIO* io, const std::string& path, const std::string& who) {
    constexpr int CUBES = VARS;
    constexpr int PRODUCT = CUBES * CUBES;
    constexpr int K = karp_luby_trials(VARS, EPSILON, DELTA);

    dimacs_dnf::Dnf<VARS, CUBES> my_dnf;
    try {
        my_dnf = dimacs_dnf::parse<VARS, CUBES>(path);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        std::exit(1);
    }

    // Only the protocol call itself is timed/counted -- not the DIMACS
    // parse above (local, pre-network data prep) -- matching emp-tool's
    // own bench convention of not billing unrelated setup work to the
    // timed call. io->sync() (a 1-byte ping/pong) lines both parties up
    // before the clock starts, so one party can't race ahead between
    // sweep points and skew the next config's timing.
    io->sync();
    uint64_t sent0 = io->send_counter, recv0 = io->recv_counter, rounds0 = io->rounds;
    auto start = clock_start();
    // unsigned __int128, not uint64_t: emp::UInt_T's reveal ceiling is 64
    // bits -- see pipeline/karp_luby_pipeline.h's top comment.
    PipelineBreakdown breakdown;
    unsigned __int128 estimate_raw = run_karp_luby_pipeline<VARS, CUBES, K>(sess, my_dnf, io, &breakdown);
    double elapsed_us = time_from(start);
    uint64_t sent = io->send_counter - sent0;
    uint64_t recv = io->recv_counter - recv0;
    uint64_t rounds = io->rounds - rounds0;

    double estimate = (double)estimate_raw / (double)((uint64_t)K * lookup_scale<PRODUCT>());

    std::cout << who << " VARS=" << VARS << " CUBES=" << CUBES
              << "  epsilon=" << EPSILON << " delta=" << DELTA << " K=" << K
              << "  estimate=" << estimate
              << "  elapsed=" << (elapsed_us / 1000.0) << " ms"
              << "  sent=" << format_bytes(sent) << " recv=" << format_bytes(recv) << " rounds=" << rounds
              << std::endl;

    print_breakdown(who, breakdown, pipeline_memory_report<VARS, CUBES, K>());
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <party: 1=ALICE, 2=BOB> <path-to-dimacs-dnf-file>\n";
        return 1;
    }

    // The VARS=16 sweep point needs far more than the 8 MiB Linux default
    // stack: pipeline/karp_luby_pipeline.h's reciprocals local is an array
    // of K=508008 elements there -- well over a hundred megabytes. Raise
    // the soft limit up front -- on Linux, the main thread's stack grows
    // on demand up to whatever RLIMIT_STACK is at fault time (not just at
    // exec time), so doing this here, after the process has already
    // started, still works.
    rlimit rl{};
    getrlimit(RLIMIT_STACK, &rl);
    rl.rlim_cur = std::min(rl.rlim_max, (rlim_t)(256ull * 1024 * 1024));
    setrlimit(RLIMIT_STACK, &rl);

    int party = parse_party(argv);
    std::string path = argv[2];

    int port = peer_port();
    auto io = (party == ALICE) ? NetIO::listen(port)
                                : NetIO::connect(peer_ip(), port);

    SH2PCSession sess(io.get(), party);
    std::string who = (party == ALICE) ? "[alice]" : "[bob]  ";

    run_one<4>(sess, io.get(), path, who);
    run_one<8>(sess, io.get(), path, who);
    run_one<16>(sess, io.get(), path, who);

    sess.finalize();
    return 0;
}
