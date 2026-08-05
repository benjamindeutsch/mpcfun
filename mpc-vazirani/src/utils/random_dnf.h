// Random DNF pair generator for benchmarking: produces Alice's and Bob's
// dimacs_dnf::Dnf<VARS,CUBES> directly (no file I/O), so bench/bench_common.h
// can size a fresh random pair to each sweep point instead of reusing one
// small fixed file via padding (see utils/dimacs_dnf.h's own padding for
// why that reuse trick existed in the first place).
//
// max_overlap bounds the number of *literals* -- not assignments -- that
// agree between any cube of Alice's and any cube of Bob's: variable v
// counts toward cube_a/cube_b's overlap iff both cubes constrain v
// (mask_a[v] && mask_b[v]) and agree on its polarity (bits_a[v] ==
// bits_b[v]). This is a purely syntactic notion (deliberately not the
// pairwise-AND model count gadgets/general/cube_weight.h's weight formula
// would give -- that's a different, weight-based quantity), chosen because
// it's cheap to bound by construction: unlike the model-count version, an
// exact per-pair literal cap can be enforced by only ever *removing* mask
// bits (never adding or flipping polarity), which is monotone and always
// terminates (see generate_random_dnf_pair's repair loop below).
//
// Deliberately has no emp-tool dependency, matching dimacs_dnf.h: this is
// data preparation, not a circuit, and needs to run identically in both
// bench_vazirani.cpp and bench_compare.cpp before either touches the
// network.
//
// Determinism: both parties' processes call this with the same
// VARS/CUBES/max_overlap/seed (all public, like every other circuit-shape
// constant in this codebase -- see README.md's "Putting it together"), so
// both independently reproduce the exact same (alice, bob) pair without
// exchanging anything -- each process then only reads its own half. This
// is a synthetic-benchmark-only convenience: unlike a real deployment,
// nothing here is a private input, so there's no leak in one process being
// able to compute both.

#pragma once

#include "utils/dimacs_dnf.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace random_dnf {

template <int VARS, int CUBES>
struct DnfPair {
    dimacs_dnf::Dnf<VARS, CUBES> alice;
    dimacs_dnf::Dnf<VARS, CUBES> bob;
};

namespace detail {

// One cube, each variable independently constrained with probability
// `density` and, if constrained, given a uniformly random polarity.
template <int VARS>
dimacs_dnf::Cube<VARS> random_cube(std::mt19937_64& rng, double density) {
    std::bernoulli_distribution constrained(density);
    std::bernoulli_distribution polarity(0.5);
    dimacs_dnf::Cube<VARS> c;
    for (int v = 0; v < VARS; ++v) {
        if (constrained(rng)) {
            c.mask[(std::size_t)v] = true;
            c.bits[(std::size_t)v] = polarity(rng);
        }
    }
    return c;
}

// Number of variables where `a` and `b` are both constrained and agree on
// polarity -- the literal-overlap metric this file's top comment defines.
template <int VARS>
int literal_overlap(const dimacs_dnf::Cube<VARS>& a, const dimacs_dnf::Cube<VARS>& b) {
    int n = 0;
    for (int v = 0; v < VARS; ++v)
        if (a.mask[(std::size_t)v] && b.mask[(std::size_t)v] && a.bits[(std::size_t)v] == b.bits[(std::size_t)v])
            ++n;
    return n;
}

// Repairs `cand` in place so its literal_overlap with every cube in
// `others` is <= max_overlap, by clearing mask bits at matching-literal
// positions (never setting a bit or flipping a polarity). Clearing a mask
// bit can only ever *remove* a match against every other cube (a match
// requires the bit to still be set on both sides), so fixing cube i can
// never re-violate the bound already established for cube j < i --
// there's no back-and-forth, and each pass that changes anything strictly
// shrinks cand's own popcount(mask), so the outer loop terminates within
// at most VARS passes.
template <int VARS, int CUBES>
void repair_overlap(std::mt19937_64& rng, dimacs_dnf::Cube<VARS>& cand,
                     const dimacs_dnf::Dnf<VARS, CUBES>& others, int max_overlap) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& other : others.cubes) {
            std::vector<int> matches;
            for (int v = 0; v < VARS; ++v)
                if (cand.mask[(std::size_t)v] && other.mask[(std::size_t)v] &&
                    cand.bits[(std::size_t)v] == other.bits[(std::size_t)v])
                    matches.push_back(v);
            if ((int)matches.size() <= max_overlap) continue;

            std::shuffle(matches.begin(), matches.end(), rng);
            int to_clear = (int)matches.size() - max_overlap;
            for (int i = 0; i < to_clear; ++i) {
                // Clear bits alongside mask: every cube type in this
                // codebase (dimacs_dnf::Cube, CircuitCube) is only ever
                // valid with bits[v]=false wherever mask[v]=false --
                // conjoin() and random_assignment() both rely on that
                // invariant (an unconstrained variable must read as 0, not
                // carry a stale polarity), so un-constraining a variable
                // here has to reset both fields together, not just mask.
                cand.mask[(std::size_t)matches[(std::size_t)i]] = false;
                cand.bits[(std::size_t)matches[(std::size_t)i]] = false;
            }
            changed = true;
        }
    }
}

}  // namespace detail

// Generates Alice's CUBES cubes with no constraint on each other (overlap
// is only defined *across* parties), then Bob's CUBES cubes, each sampled
// the same way and then repaired (detail::repair_overlap) against every
// one of Alice's cubes so every cross-party cube pair's literal overlap is
// <= max_overlap (clamped to [0, VARS] -- a cap outside that range is
// either vacuous or unsatisfiable-by-construction). Both Dnfs come back
// with exactly CUBES real (non-pad) cubes -- generation always produces
// exactly the requested count, unlike dimacs_dnf::parse, which pads only
// when a file happens to declare fewer.
template <int VARS, int CUBES>
DnfPair<VARS, CUBES> generate_random_dnf_pair(int max_overlap, uint64_t seed, double density = 0.5) {
    static_assert(VARS > 0, "random_dnf::generate_random_dnf_pair: VARS must be positive");
    static_assert(CUBES > 0, "random_dnf::generate_random_dnf_pair: CUBES must be positive");
    max_overlap = std::clamp(max_overlap, 0, VARS);

    std::mt19937_64 rng(seed);
    DnfPair<VARS, CUBES> pair;

    for (int i = 0; i < CUBES; ++i)
        pair.alice.cubes[(std::size_t)i] = detail::random_cube<VARS>(rng, density);

    for (int i = 0; i < CUBES; ++i) {
        dimacs_dnf::Cube<VARS> cand = detail::random_cube<VARS>(rng, density);
        detail::repair_overlap<VARS, CUBES>(rng, cand, pair.alice, max_overlap);
        pair.bob.cubes[(std::size_t)i] = cand;
    }

    return pair;
}

}  // namespace random_dnf
