// Plaintext ground-truth sanity check for the benchmarks: builds the exact
// (host-side, non-circuit) conjunction of two generated dimacs_dnf::Dnfs
// (see utils/random_dnf.h), writes it as a DIMACS-DNF file, and shells out
// to this repo's bundled `pepin` binary (an approximate DNF model counter,
// see README.md) to get an independent estimate of the true satisfying-
// assignment count -- something to compare the protocol's own revealed
// estimate against (see bench/bench_common.h's run_one).
//
// Deliberately has no emp-tool dependency: everything here is plain-C++
// data prep on dimacs_dnf::Cube (vector<bool>-backed), mirroring
// gadgets/general/dnf_distribute.h's conjoin/conjoin_dnf logic exactly but
// without a BooleanContext -- this is a sanity-check oracle computed
// outside the circuit, not a replacement for it (comparing the two is the
// whole point).

#pragma once

#include "utils/dimacs_dnf.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace ground_truth {

// a /\ b, mirroring gadgets/general/dnf_distribute.h's conjoin() bit for
// bit: padding is contagious (pad=true, bits pinned to 1^N, mask pinned to
// 0^N) if either input is already padding, or if a/b disagree on some
// variable they both constrain (an unsatisfiable conjunction); otherwise
// mask = a.mask | b.mask, bits = a's value wherever a constrains that
// variable, else b's.
template <int VARS>
dimacs_dnf::Cube<VARS> plain_conjoin(const dimacs_dnf::Cube<VARS>& a, const dimacs_dnf::Cube<VARS>& b) {
    bool disagree = false;
    for (int v = 0; v < VARS; ++v)
        if (a.mask[(std::size_t)v] && b.mask[(std::size_t)v] && a.bits[(std::size_t)v] != b.bits[(std::size_t)v])
            disagree = true;

    dimacs_dnf::Cube<VARS> out;
    out.pad = a.pad || b.pad || disagree;
    if (out.pad) {
        out.bits.fill(true);
        out.mask.fill(false);
        return out;
    }
    for (int v = 0; v < VARS; ++v) {
        out.mask[(std::size_t)v] = a.mask[(std::size_t)v] || b.mask[(std::size_t)v];
        out.bits[(std::size_t)v] = a.mask[(std::size_t)v] ? a.bits[(std::size_t)v] : b.bits[(std::size_t)v];
    }
    return out;
}

// The cubes of d1 /\ d2 -- CUBES*CUBES pairwise conjunctions, same
// cross-product order as gadgets/general/dnf_distribute.h's conjoin_dnf.
// Contradictory/padding pairings come back flagged pad=true rather than
// filtered here -- write_dimacs_dnf below is what actually drops them.
template <int VARS, int CUBES>
std::array<dimacs_dnf::Cube<VARS>, (std::size_t)CUBES * (std::size_t)CUBES> plain_conjoin_dnf(
        const dimacs_dnf::Dnf<VARS, CUBES>& d1, const dimacs_dnf::Dnf<VARS, CUBES>& d2) {
    std::array<dimacs_dnf::Cube<VARS>, (std::size_t)CUBES * (std::size_t)CUBES> out;
    std::size_t idx = 0;
    for (const auto& a : d1.cubes)
        for (const auto& b : d2.cubes) out[idx++] = plain_conjoin<VARS>(a, b);
    return out;
}

// Writes `cubes` as a DIMACS-DNF file, skipping padding cubes entirely: a
// padding cube isn't a real disjunct (see utils/dimacs_dnf.h), and writing
// it literally (as an always-true, fully unconstrained cube) would make
// pepin count every one of 2^VARS assignments as satisfying, which is
// wrong. Returns the number of real (non-pad) cubes written -- 0 if every
// cube in `cubes` was padding (the conjunction is unsatisfiable), in which
// case run_pepin_sanity_check below skips invoking pepin at all (an empty
// "p dnf V 0" file makes pepin itself error out).
template <int VARS, std::size_t M>
int write_dimacs_dnf(const std::string& path, const std::array<dimacs_dnf::Cube<VARS>, M>& cubes) {
    std::vector<const dimacs_dnf::Cube<VARS>*> real;
    for (const auto& c : cubes)
        if (!c.pad) real.push_back(&c);

    std::ofstream out(path);
    out << "p dnf " << VARS << " " << real.size() << "\n";
    for (const auto* c : real) {
        for (int v = 0; v < VARS; ++v)
            if (c->mask[(std::size_t)v]) out << (c->bits[(std::size_t)v] ? (v + 1) : -(v + 1)) << " ";
        out << "0\n";
    }
    return (int)real.size();
}

// estimate: pepin's own "Low-precision approx num points" value.
// elapsed_us: wall-clock time of the subprocess call itself (process
// spawn + pepin's own counting + teardown), not just its internal "T:"
// line (which only covers its counting phase, and is only ever printed
// with two decimal digits -- too coarse to show anything below ~5ms; see
// run_pepin below for why this project measures around the call instead).
struct PepinResult {
    double estimate;
    double elapsed_us;
};

// Shells out to the pepin approximate DNF model counter on the file at
// `path` and parses its "Low-precision approx num points:" line -- the
// number pepin reports is itself already an approximation of the file's
// true satisfying-assignment count, which is all this project needs it
// for (an independent, non-circuit cross-check of the protocol's own
// revealed estimate, not an exact oracle). Returns nullopt if the binary
// couldn't be run or its output didn't contain that line (e.g. a
// missing/incompatible pepin build), so callers degrade to skipping the
// sanity-check line rather than crashing the benchmark over an optional
// check.
//
// Timed with std::chrono::steady_clock around the whole popen/pclose
// round trip (not emp::clock_start/time_from -- this file is deliberately
// emp-tool-free, see its own top comment), so the reported time includes
// process spawn/teardown overhead alongside pepin's own counting, i.e.
// what actually shows up as extra wall-clock cost on top of the timed
// protocol call at each sweep point.
//
// PEPIN_PATH overrides the binary location (same env-var convention as
// emp-tool's own EMP_PORT/EMP_PEER_IP, see emp-tool/runtime/core/utils.h),
// defaulting to "./pepin" -- this repo's own bundled binary, expected to
// be run from the project root (matching run.sh's own convention).
inline std::optional<PepinResult> run_pepin(const std::string& path, double epsilon = 0.1, double delta = 0.05,
                                             uint64_t seed = 1) {
    const char* bin = std::getenv("PEPIN_PATH");
    if (!bin) bin = "./pepin";

    std::ostringstream cmd;
    cmd << bin << " -e " << epsilon << " -d " << delta << " -s " << seed << " " << path << " 2>/dev/null";

    auto start = std::chrono::steady_clock::now();
    FILE* p = popen(cmd.str().c_str(), "r");
    if (!p) return std::nullopt;

    std::string output;
    char buf[4096];
    while (fgets(buf, sizeof(buf), p)) output += buf;
    pclose(p);
    double elapsed_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();

    const std::string marker = "Low-precision approx num points:";
    std::size_t pos = output.find(marker);
    if (pos == std::string::npos) return std::nullopt;
    return PepinResult{std::strtod(output.c_str() + pos + marker.size(), nullptr), elapsed_us};
}

// Ties the pieces above together: given both parties' already-generated
// Dnfs (see utils/random_dnf.h -- only meaningful in this synthetic
// benchmark setting, where one process can see both; see that file's own
// comment on why that isn't a real-deployment privacy leak), builds the
// plaintext conjunction, writes it to a unique temp file, runs pepin on
// it, and returns its ground-truth estimate plus how long pepin itself
// took (0 in the unsatisfiable case below, where pepin is never actually
// invoked).
template <int VARS, int CUBES>
std::optional<PepinResult> run_pepin_sanity_check(const dimacs_dnf::Dnf<VARS, CUBES>& alice,
                                                   const dimacs_dnf::Dnf<VARS, CUBES>& bob) {
    auto conjunction = plain_conjoin_dnf<VARS, CUBES>(alice, bob);

    char path_template[] = "/tmp/mpc_vazirani_ground_truth_XXXXXX";
    int fd = mkstemp(path_template);
    if (fd < 0) return std::nullopt;
    close(fd);
    std::string path = path_template;

    int real_cubes = write_dimacs_dnf<VARS>(path, conjunction);
    std::optional<PepinResult> result = (real_cubes > 0) ? run_pepin(path) : std::optional<PepinResult>(PepinResult{0.0, 0.0});

    std::remove(path.c_str());
    return result;
}

}  // namespace ground_truth
