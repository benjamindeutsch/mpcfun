// Unit tests for utils/dimacs_dnf.h. Pure stdlib, no emp-tool dependency
// (same reason the parser itself has none): each case writes a small
// DIMACS-DNF snippet to a temp file, parses it, and checks the result --
// including every error path -- against hand-computed expectations.
//
// run_dimacs_dnf_tests() is called from tests/run_tests.cpp's main(), the
// single entry point for every *_test.cpp under tests/ -- see that file.

#include "utils/dimacs_dnf.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

namespace {

template <int N>
std::array<bool, N> bits_of(const char* s) {
    std::array<bool, N> b{};
    for (int i = 0; i < N; ++i) b[(std::size_t)i] = (s[i] == '1');
    return b;
}

std::filesystem::path write_temp(const std::string& name, const std::string& content) {
    std::filesystem::path p = std::filesystem::temp_directory_path() / ("dimacs_dnf_test_" + name + ".cnf");
    std::ofstream out(p);
    out << content;
    return p;
}

void expect(bool cond, const char* name) {
    if (!cond) {
        std::fprintf(stderr, "FAIL %s\n", name);
        std::exit(1);
    }
    std::printf("PASS %s\n", name);
}

void expect_throws(const std::function<void()>& fn, const char* name) {
    try {
        fn();
    } catch (const std::runtime_error&) {
        std::printf("PASS %s\n", name);
        return;
    }
    std::fprintf(stderr, "FAIL %s (expected a throw, got none)\n", name);
    std::exit(1);
}

}  // namespace

int run_dimacs_dnf_tests() {
    // Mirrors sample.dnf.cnf: 5 real vars/3 real cubes, parsed with headroom
    // (VARS=8, CUBES=5) to also exercise variable zero-extension and cube
    // padding in the same case.
    {
        auto path = write_temp("basic",
                                "c comment, ignored\n"
                                "p dnf 5 3\n"
                                "1 -3 5 0\n"
                                "-2 4 0\n"
                                "0\n");
        auto dnf = dimacs_dnf::parse<8, 5>(path.string());

        expect(dnf.cubes[0].bits == bits_of<8>("10001000"), "basic: cube0 bits (x1 /\\ ~x3 /\\ x5)");
        expect(dnf.cubes[0].mask == bits_of<8>("10101000"), "basic: cube0 mask");
        expect(!dnf.cubes[0].pad, "basic: cube0 is not padding");

        expect(dnf.cubes[1].bits == bits_of<8>("00010000"), "basic: cube1 bits (~x2 /\\ x4)");
        expect(dnf.cubes[1].mask == bits_of<8>("01010000"), "basic: cube1 mask");
        expect(!dnf.cubes[1].pad, "basic: cube1 is not padding");

        expect(dnf.cubes[2].bits == bits_of<8>("00000000"), "basic: cube2 (empty cube) bits all-0");
        expect(dnf.cubes[2].mask == bits_of<8>("00000000"), "basic: cube2 (empty cube) mask all-0");
        expect(!dnf.cubes[2].pad, "basic: cube2 is a real cube, not padding");

        expect(dnf.cubes[3].pad, "basic: cube3 is padding");
        expect(dnf.cubes[3].bits == bits_of<8>("11111111"), "basic: cube3 padding bits all-1");
        expect(dnf.cubes[3].mask == bits_of<8>("00000000"), "basic: cube3 padding mask all-0");
        expect(dnf.cubes[4].pad, "basic: cube4 is padding");

        std::filesystem::remove(path);
    }

    // File exactly fills the fixed capacity: no padding cubes added.
    {
        auto path = write_temp("exact_fit", "p dnf 2 2\n1 0\n-2 0\n");
        auto dnf = dimacs_dnf::parse<2, 2>(path.string());
        expect(!dnf.cubes[0].pad && !dnf.cubes[1].pad, "exact fit: no padding cubes");
        expect(dnf.cubes[0].bits == bits_of<2>("10"), "exact fit: cube0 bits");
        expect(dnf.cubes[1].bits == bits_of<2>("00"), "exact fit: cube1 bits (~x2)");
        std::filesystem::remove(path);
    }

    // Errors: fixed-capacity violations.
    {
        auto path = write_temp("too_many_vars_header", "p dnf 20 1\n1 2 0\n");
        expect_throws([&] { dimacs_dnf::parse<8, 5>(path.string()); },
                       "error: file declares more variables than VARS");
        std::filesystem::remove(path);
    }
    {
        auto path = write_temp("too_many_cubes_header", "p dnf 3 20\n1 0\n");
        expect_throws([&] { dimacs_dnf::parse<8, 5>(path.string()); },
                       "error: file declares more cubes than CUBES");
        std::filesystem::remove(path);
    }
    {
        // Header under-declares, but the body actually has 6 cubes > CUBES=5.
        auto path = write_temp("too_many_cubes_actual", "p dnf 3 1\n1 0\n2 0\n3 0\n1 2 0\n1 3 0\n2 3 0\n");
        expect_throws([&] { dimacs_dnf::parse<8, 5>(path.string()); },
                       "error: file actually contains more cubes than CUBES");
        std::filesystem::remove(path);
    }

    // Errors: malformed content (unrelated to the fixed capacity).
    {
        auto path = write_temp("out_of_range", "p dnf 2 1\n1 5 0\n");
        expect_throws([&] { dimacs_dnf::parse<8, 5>(path.string()); },
                       "error: literal out of range 1..file_vars");
        std::filesystem::remove(path);
    }
    {
        auto path = write_temp("contradiction", "p dnf 3 1\n1 -1 2 0\n");
        expect_throws([&] { dimacs_dnf::parse<8, 5>(path.string()); },
                       "error: variable asserted both polarities in one cube");
        std::filesystem::remove(path);
    }
    {
        auto path = write_temp("no_header", "1 2 0\n");
        expect_throws([&] { dimacs_dnf::parse<8, 5>(path.string()); }, "error: literals before header");
        std::filesystem::remove(path);
    }
    {
        auto path = write_temp("truncated", "p dnf 3 1\n1 2\n");
        expect_throws([&] { dimacs_dnf::parse<8, 5>(path.string()); },
                       "error: file ends mid-cube (missing terminating 0)");
        std::filesystem::remove(path);
    }
    {
        auto path = write_temp("empty_file", "");
        expect_throws([&] { dimacs_dnf::parse<8, 5>(path.string()); }, "error: missing 'p dnf' header entirely");
        std::filesystem::remove(path);
    }
    expect_throws([&] { dimacs_dnf::parse<8, 5>("/nonexistent/path/hopefully.cnf"); },
                   "error: cannot open nonexistent file");

    std::printf("dimacs_dnf_test: all checks passed\n");
    return 0;
}
