// Parser for DIMACS files whose lines are read as DNF cubes rather than CNF
// clauses. The file syntax is unchanged from ordinary DIMACS CNF ("p dnf
// <num_vars> <num_cubes>" header, each cube a whitespace-separated list of
// literals terminated by a literal 0) -- only the semantics differ: each
// line is one cube (a conjunction of literals), and the whole file is their
// disjunction.
//
// VARS/CUBES are a fixed capacity, not read from the file: parse<VARS,CUBES>
// throws if the file needs more of either, and pads the result up to
// exactly CUBES cubes otherwise (see parse() below for why). Cube::bits/mask
// are therefore std::array<bool,VARS>, not a runtime-sized vector<bool> --
// which is also why parse() is a template defined right here rather than
// split into a .cpp: its body must be visible at every VARS/CUBES
// instantiation, same reasoning as gadgets/dnf_distribute.h.
//
// Deliberately has no emp-tool dependency: this is data preparation, not a
// circuit. Callers wire Cube::bits / Cube::mask into whatever wire type
// (e.g. emp::BitVec_T<Ctx,VARS>) their protocol uses.

#pragma once

#include <array>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace dimacs_dnf {

// For a cube c over variables x_1..x_VARS (1-indexed in the file, 0-indexed
// here: bits[i]/mask[i] describe x_{i+1}):
//   bits[i] = 1 iff the positive literal x_{i+1} is in c
//   mask[i] = 1 iff x_{i+1} or its negation appears in c (i.e. c constrains
//             that variable at all)
// A variable not mentioned in c has mask[i] == 0 and bits[i] == 0. This also
// covers variables beyond the file's own declared count, up to VARS: never
// mentioned, so left at the same "unconstrained" default.
//
// pad == true marks a cube that isn't from the file -- it's filler added by
// parse() to bring the cube count up to the fixed CUBES capacity. Its bits
// are all 1 and its mask is all 0 (an always-true, unconstrained cube), so
// it's harmless even if a caller forgets to check `pad`; checking it is
// still the caller's job wherever the real cube count must stay hidden
// (e.g. inside an MPC circuit, which is the reason a fixed CUBES exists at
// all instead of just sizing to whatever the file has).
template <int VARS>
struct Cube {
    std::array<bool, (std::size_t)VARS> bits{};
    std::array<bool, (std::size_t)VARS> mask{};
    bool pad = false;
};

template <int VARS, int CUBES>
struct Dnf {
    std::array<Cube<VARS>, (std::size_t)CUBES> cubes{};
};

// Throws std::runtime_error on: a missing/malformed "p dnf" header, the file
// declaring more than VARS variables or more than CUBES cubes, a literal
// outside 1..(file's declared var count), a cube that assigns both
// polarities to the same variable (an unsatisfiable cube -- almost
// certainly not what a caller intended), or a file that ends mid-cube (no
// terminating 0).
template <int VARS, int CUBES>
Dnf<VARS, CUBES> parse(const std::string& path) {
    static_assert(VARS > 0, "dimacs_dnf::parse: VARS must be positive");
    static_assert(CUBES > 0, "dimacs_dnf::parse: CUBES must be positive");

    std::ifstream in(path);
    if (!in) throw std::runtime_error("dimacs_dnf::parse: cannot open " + path);

    Dnf<VARS, CUBES> dnf;
    bool have_header = false;
    int file_vars = 0;
    int cube_count = 0;  // real cubes written into dnf.cubes so far
    Cube<VARS> current;
    int literals_in_current = 0;

    auto reset_current = [&] {
        current = Cube<VARS>{};
        literals_in_current = 0;
    };

    std::string line;
    while (std::getline(in, line)) {
        std::size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;  // blank line
        if (line[start] == 'c') continue;           // comment line

        if (line[start] == 'p') {
            if (have_header)
                throw std::runtime_error("dimacs_dnf::parse: duplicate 'p' line in " + path);
            std::istringstream iss(line.substr(start));
            std::string p, fmt;
            int file_cubes = 0;
            iss >> p >> fmt >> file_vars >> file_cubes;
            if (fmt != "dnf" || file_vars <= 0)
                throw std::runtime_error("dimacs_dnf::parse: malformed 'p' line in " + path);
            if (file_vars > VARS)
                throw std::runtime_error("dimacs_dnf::parse: file declares " + std::to_string(file_vars) +
                                          " variables, more than the fixed capacity " + std::to_string(VARS) +
                                          " in " + path);
            if (file_cubes > CUBES)
                throw std::runtime_error("dimacs_dnf::parse: file declares " + std::to_string(file_cubes) +
                                          " cubes, more than the fixed capacity " + std::to_string(CUBES) +
                                          " in " + path);
            have_header = true;
            reset_current();
            continue;
        }

        if (!have_header)
            throw std::runtime_error("dimacs_dnf::parse: literals before 'p dnf' header in " + path);

        std::istringstream iss(line);
        int lit;
        while (iss >> lit) {
            if (lit == 0) {
                if (cube_count >= CUBES)
                    throw std::runtime_error("dimacs_dnf::parse: " + path +
                                              " contains more cubes than the fixed capacity " +
                                              std::to_string(CUBES));
                dnf.cubes[(std::size_t)cube_count] = current;
                ++cube_count;
                reset_current();
                continue;
            }
            int var = lit > 0 ? lit : -lit;
            if (var < 1 || var > file_vars)
                throw std::runtime_error("dimacs_dnf::parse: literal " + std::to_string(lit) +
                                          " out of range 1.." + std::to_string(file_vars) + " in " + path);
            std::size_t idx = (std::size_t)(var - 1);
            if (current.mask[idx])
                throw std::runtime_error("dimacs_dnf::parse: variable " + std::to_string(var) +
                                          " appears with both polarities in the same cube in " + path);
            current.mask[idx] = true;
            current.bits[idx] = (lit > 0);
            ++literals_in_current;
        }
    }

    if (!have_header)
        throw std::runtime_error("dimacs_dnf::parse: missing 'p dnf' header in " + path);
    if (literals_in_current > 0)
        throw std::runtime_error("dimacs_dnf::parse: file ends mid-cube (missing terminating 0) in " + path);

    // Fewer real cubes than the fixed capacity: pad with always-true,
    // unconstrained cubes flagged pad=true.
    for (; cube_count < CUBES; ++cube_count) {
        Cube<VARS> pad_cube;
        pad_cube.bits.fill(true);
        pad_cube.mask.fill(false);
        pad_cube.pad = true;
        dnf.cubes[(std::size_t)cube_count] = pad_cube;
    }

    return dnf;
}

}  // namespace dimacs_dnf
