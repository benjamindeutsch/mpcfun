// Single entry point for every unit test under tests/. Each *_test.cpp
// exposes one run_*_tests() function (its former main()) instead of its own
// main -- adding a new test file means adding one declaration and one call
// here, not a new CMake executable target. Each suite currently exits(1)
// immediately on its first failed check (see the *_test.cpp files), so
// reaching the next call here already means the previous suite passed in
// full.

#include <cstdio>

int run_dimacs_dnf_tests();
int run_dnf_distribute_tests();
int run_cube_weight_tests();
int run_dnf_weight_tests();
int run_cube_intervals_tests();
int run_select_cube_tests();

int main() {
    std::printf("=== dimacs_dnf_test ===\n");
    run_dimacs_dnf_tests();

    std::printf("=== dnf_distribute_test ===\n");
    run_dnf_distribute_tests();

    std::printf("=== cube_weight_test ===\n");
    run_cube_weight_tests();

    std::printf("=== dnf_weight_test ===\n");
    run_dnf_weight_tests();

    std::printf("=== cube_intervals_test ===\n");
    run_cube_intervals_tests();

    std::printf("=== select_cube_test ===\n");
    run_select_cube_tests();

    std::printf("=== all test suites passed ===\n");
    return 0;
}
