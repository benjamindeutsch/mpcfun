cmake_minimum_required(VERSION 3.16)
project(mpc-sandbox CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# emp-sh2pc's public header requires C++20 (it is set as a transitive compile
# feature on the emp-sh2pc target below), so this is mostly documentation.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# emp-tool/emp-ot/emp-sh2pc were installed to a user prefix in this sandbox
# (see README.md) rather than /usr/local, so make sure their *-config.cmake
# files are discoverable. This has no effect if they're already on the
# default CMake search path (e.g. installed system-wide via /usr/local).
list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local")

find_package(emp-tool 1.0 REQUIRED)
find_package(emp-ot 1.0 REQUIRED)
find_package(emp-sh2pc REQUIRED)
find_package(OpenSSL REQUIRED)

# GMP has no upstream CMake package config on Fedora (gmp-devel just ships
# headers + .so), so locate it manually. Not used by the sanity-check demo
# itself -- emp's garbled-circuit path is self-contained -- but wired up here
# since the larger MPC project this sandbox is a foundation for will likely
# need big-integer arithmetic.
find_path(GMP_INCLUDE_DIR NAMES gmp.h)
find_library(GMP_LIBRARY NAMES gmp)
find_library(GMPXX_LIBRARY NAMES gmpxx)
if(NOT GMP_INCLUDE_DIR OR NOT GMP_LIBRARY)
    message(FATAL_ERROR "GMP not found. On Fedora: sudo dnf install gmp-devel gmp-c++")
endif()

# src/ itself is on the include path so headers are referenced by their
# folder (gadgets/..., utils/...) from anywhere under src/, e.g. in tests/.
set(MPC_SANDBOX_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(sh2pc_demo src/main.cpp)
target_link_libraries(sh2pc_demo PRIVATE
    emp-sh2pc::emp-sh2pc
    emp-ot::emp-ot
    emp-tool::emp-tool
    OpenSSL::Crypto
    ${GMP_LIBRARY}
    ${GMPXX_LIBRARY}
)
target_include_directories(sh2pc_demo PRIVATE ${GMP_INCLUDE_DIR} ${MPC_SANDBOX_SRC_DIR})

# One test binary for every tests/*_test.cpp (see run_tests.cpp): each
# exposes a run_*_tests() called from run_tests.cpp's main(), so a new test
# file only needs adding to this source list + a declaration+call in
# run_tests.cpp, not a whole new executable target. dnf_distribute_test.cpp
# needs emp-tool (it drives gadgets/dnf_distribute.h through
# emp::ClearSession); dimacs_dnf_test.cpp doesn't, but sharing one binary
# means it now links emp-tool too -- the trade-off for a single executable.
add_executable(run_tests
    src/tests/run_tests.cpp
    src/tests/dimacs_dnf_test.cpp
    src/tests/dnf_distribute_test.cpp
    src/tests/cube_weight_test.cpp
    src/tests/dnf_weight_test.cpp
    src/tests/cube_intervals_test.cpp
)
target_link_libraries(run_tests PRIVATE emp-tool::emp-tool)
target_include_directories(run_tests PRIVATE ${MPC_SANDBOX_SRC_DIR})
