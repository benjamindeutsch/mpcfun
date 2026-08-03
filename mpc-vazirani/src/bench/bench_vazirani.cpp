// bench_vazirani: times the real two-party Vazirani circuit
// (pipeline/vazirani_pipeline.h) across a sweep of (VARS,CUBES) sizes
// (VARS=CUBES, doubling from 4 to 32), each at a *fixed*, meaningful
// epsilon/delta -- ApproxMC's own defaults, epsilon=0.8 (tolerance) and
// delta=0.2 (failure probability, i.e. success probability >= 1-delta =
// 0.8).
//
// K = gadgets::vazirani_trials(CUBES, epsilon, delta), which (after an
// earlier bug -- see that function's own comment) is:
//
//   K = ceil(min(1/delta, 3*ln(2/delta)) * (CUBES^2 - 1) / epsilon^2)
//
// linear in CUBES^2 (the number of cubes in the conjoined DNF), not
// quadratic in it -- that linearity is the actual Vazirani theorem. An
// earlier, incorrect version of vazirani_trials was quadratic in
// (VARS^2-1) instead, which made K explode as O(VARS^4) and made anything
// past VARS=16 intractable (billions of trials, hours to months). The
// corrected, linear formula is cheap enough to sweep all the way to
// VARS=32 in under two minutes total, measured live:
//
//   CUBES   M=CUBES^2   K       K*M (work)   measured elapsed
//   4       16          118     1888         34.48 ms
//   8       64          493     31552        480.12 ms
//   16      256         1993    510208       5.88 s
//   32      1024        7993    8184832      1.40 min
//
// (`c = min(1/delta, 3*ln(2/delta))` happens to be `5` -- i.e. Chebyshev,
// not the Chernoff alternative -- at this epsilon/delta; see
// vazirani_trials' own comment for when each wins.)
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
//   ./bench_vazirani <party: 1=ALICE, 2=BOB> <path-to-dimacs-dnf-file>
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

#include "pipeline/vazirani_pipeline.h"

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

// us -> the largest whole unit that keeps it readable (us/ms/s/min/h) --
// std::ostream's default double formatting switches to scientific
// notation past ~1e6 (e.g. "1.07004e+06 ms" for VARS=16's ~18-minute
// elapsed time), which is exactly the un-readable case this avoids.
std::string format_duration(double us) {
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
    // CUBES, not VARS: vazirani_trials(cubes, ...) bounds K by the number
    // of cubes in the conjoined DNF (CUBES^2 - 1, per its own comment),
    // not by VARS -- they happen to be equal in this sweep (VARS=CUBES
    // above), but CUBES is the semantically correct argument.
    constexpr int K = vazirani_trials(CUBES, EPSILON, DELTA);

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
    // bits -- see pipeline/vazirani_pipeline.h's top comment.
    PipelineBreakdown breakdown;
    unsigned __int128 estimate_raw = run_vazirani_pipeline<VARS, CUBES, K>(sess, my_dnf, io, &breakdown);
    double elapsed_us = time_from(start);
    uint64_t sent = io->send_counter - sent0;
    uint64_t recv = io->recv_counter - recv0;
    uint64_t rounds = io->rounds - rounds0;

    double estimate = (double)estimate_raw / (double)((uint64_t)K * lookup_scale<PRODUCT>());

    std::cout << who << " VARS=" << VARS << " CUBES=" << CUBES
              << "  epsilon=" << EPSILON << " delta=" << DELTA << " K=" << K
              << "  estimate=" << estimate
              << "  elapsed=" << format_duration(elapsed_us)
              << "  sent=" << format_bytes(sent) << " recv=" << format_bytes(recv) << " rounds=" << rounds
              << std::endl;

    print_breakdown(who, breakdown, pipeline_memory_report<VARS, CUBES, K>());
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <party: 1=ALICE, 2=BOB> <path-to-dimacs-dnf-file>\n";
        return 1;
    }

    // None of this sweep's current sizes actually need more than the 8 MiB
    // Linux default stack any more (vazirani_trials' K values are all
    // small now -- see this file's top comment), but pipeline/
    // vazirani_pipeline.h's conjunction/weights/intervals locals scale
    // with M=CUBES^2 regardless of K, and would need this again well
    // before a hypothetical VARS=64 sweep point. Raise the soft limit up
    // front as cheap headroom -- on Linux, the main thread's stack grows
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
    run_one<32>(sess, io.get(), path, who);
    run_one<64>(sess, io.get(), path, who);

    sess.finalize();
    return 0;
}
