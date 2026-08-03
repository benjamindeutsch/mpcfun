// bits_of<N>(s): converts a '0'/'1' string into a std::array<bool,N> the
// same way every test under tests/ needs to turn a human-readable cube/
// assignment string into wire-input data. Pure stdlib, no emp-tool
// dependency -- shared even by dimacs_dnf_test.cpp, which deliberately
// stays emp-tool-free (matching the parser it tests). See
// tests/test_helpers.h for the emp-tool dependent helpers built on top of
// this (bits_of_uint, make_cube).

#pragma once

#include <array>
#include <cstddef>

template <int N>
std::array<bool, N> bits_of(const char* s) {
    std::array<bool, N> b{};
    for (int i = 0; i < N; ++i) b[(std::size_t)i] = (s[i] == '1');
    return b;
}
