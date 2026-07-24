# mpc-sandbox

Minimal two-party garbled-circuit sanity check built on
[emp-toolkit](https://github.com/emp-toolkit) (`emp-tool` + `emp-ot` +
`emp-sh2pc`, semi-honest 2PC). Not related to any real MPC protocol logic --
it just proves the whole pipeline (garbling, OT, network setup, evaluation,
output reveal) works end to end before building anything real on top of it.

Alice and Bob each hold a private 32-bit bitstring; the circuit reveals
bitwise AND, OR, XOR, NOT (of Alice's bits), and equality to both parties.
Everything is over `emp::BitVec_T` -- pure logical wires, no `UInt_T`
arithmetic anywhere.

## Prerequisites

emp-tool, emp-ot, and emp-sh2pc must already be built and installed (see the
top-level notes on how this sandbox's environment was set up -- they were
installed to `~/.local` in this environment, not `/usr/local`, so no `sudo`
was needed for that part). `CMakeLists.txt` adds `~/.local` to
`CMAKE_PREFIX_PATH` automatically; if you installed them system-wide instead,
that line is a no-op.

Also needed: OpenSSL and GMP development headers (`gmp-devel` on Fedora --
GMP isn't actually used by this demo, but is wired into the build since a
larger MPC project will likely need it).

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Produces a single executable, `build/sh2pc_demo`, taking a `--party`-style
positional arg rather than separate alice/bob binaries (this matches how
emp-sh2pc's own tests are structured).

## Run

```sh
./build/sh2pc_demo <party: 1=ALICE, 2=BOB> <secret-32-bit-string>
```

The bitstring must be exactly 32 characters of `0`/`1`.

Two terminals:

```sh
# terminal 1
./build/sh2pc_demo 1 11001100110011001100110011001100

# terminal 2
./build/sh2pc_demo 2 10101010101010101010101010101010
```

Or both at once via the helper script (`./run.sh <alice-bits> <bob-bits>`,
both args optional):

```sh
./run.sh
# [alice] my bits=11001100110011001100110011001100  AND=10001000100010001000100010001000  OR=11101110111011101110111011101110  XOR=01100110011001100110011001100110  NOT(a)=00110011001100110011001100110011  a==b=false
# [bob]   my bits=10101010101010101010101010101010  AND=10001000100010001000100010001000  OR=11101110111011101110111011101110  XOR=01100110011001100110011001100110  NOT(a)=00110011001100110011001100110011  a==b=false
```

Alice listens on a fixed localhost port (default `12345`); Bob connects to
it. Override with the `EMP_PORT` (port) and `EMP_PEER_IP` (Bob's target
address) environment variables if needed.

## DIMACS-DNF cube parser

`src/utils/dimacs_dnf.h` parses a DIMACS file whose lines are read as DNF
cubes rather than CNF clauses (same file syntax as DIMACS CNF otherwise --
`p dnf <vars> <cubes>` header, each line a literal list terminated by `0`).
For each cube it builds a bit vector (which variables are asserted
positively) and a mask (which variables the cube constrains at all).
Deliberately has no emp-tool dependency -- it's data preparation, not a
circuit; a caller wires `Cube::bits`/`Cube::mask` into whatever wire type
(e.g. `emp::BitVec_T`) a future protocol needs.

`VARS`/`CUBES` are a fixed *capacity*, given as template parameters to
`parse<VARS,CUBES>` -- not read from the file. `Cube::bits`/`mask` are
`std::array<bool,VARS>` (not a runtime-sized `vector<bool>`), and the result
always has exactly `CUBES` cubes: `parse` throws if the file needs more of
either, and otherwise pads the result up to `CUBES` with cubes marked
`pad=true` (`bits` all `1`, `mask` all `0` -- an always-true, unconstrained
filler cube). This is for a future circuit that must hide how many cubes/vars
a party's real DNF actually has -- padding to a fixed public size is the
usual way to do that in MPC. Being a template (so it can be instantiated at
whatever `VARS`/`CUBES` a caller needs) is also why `parse` is defined
directly in the header with no companion `.cpp`, for the same reason as
`gadgets/dnf_distribute.h` (see its file for the fuller explanation).

`src/tests/dimacs_dnf_test.cpp` is its unit test (run via `build/run_tests`,
see "Tests" below): writes small DIMACS-DNF snippets to temp files and
checks the parse against hand-computed expectations, including every error
path. Its own code stays pure stdlib -- no emp-tool -- same as the parser
itself; only the shared test binary it's built into links emp-tool, for the
other suite's sake.

(`sample.dnf.cnf` at the project root is a standalone example of the file
format -- 5 vars / 3 cubes: cube 0 is `x1 ∧ ¬x3 ∧ x5`, cube 1 is `¬x2 ∧ x4`,
cube 2 is the empty cube, always true. The test above embeds the same
content inline rather than reading that file, so it isn't tied to a
particular working directory.)

## Circuit gadgets

`src/gadgets/circuit_cube.h` defines `CircuitCube<Ctx,N>{bits, mask, pad}`,
the same fields as `dimacs_dnf::Cube` but as wires
(`emp::BitVec_T<Ctx,N>`/`emp::Bit_T<Ctx>`) instead of `vector<bool>`/`bool`,
plus the `CubeWeight<Ctx,N>` alias used by `cube_weight.h`/`dnf_weight.h`.
Both are shared foundational types, not specific to any one gadget -- they
live in their own file (rather than, say, inside `dnf_distribute.h` or
`cube_weight.h`) for exactly that reason.

`src/gadgets/dnf_distribute.h`'s `conjoin`/`conjoin_dnf` use `CircuitCube`
for both inputs and outputs -- there's no separate "result" type, since a
conjunction's output has the same shape as an input (and can itself be
conjoined further).

`conjoin(a, b)` computes `a /\ b`. Padding is contagious: the result is
padding (`pad=true`, `bits` pinned to `1^N`, `mask` pinned to `0^N`) if
*either* input is already padding, or if `a`/`b` disagree on some variable
they both constrain (making the conjunction unsatisfiable -- there's no
"no such cube" value, so that has to be flagged the same way as padding).
Otherwise it's the ordinary per-bit-mux conjunction: `mask = a.mask |
b.mask`, `bits = (a.bits & a.mask) | (b.bits & ~a.mask)` (take `a`'s value
wherever `a` constrains that variable, else `b`'s). `conjoin_dnf<Ctx,N,
CUBES>(d1, d2)` takes two `std::array<CircuitCube<Ctx,N>, CUBES>` and
cross-products every cube in `d1` against every cube in `d2` into a
`std::array<CircuitCube<Ctx,N>, CUBES*CUBES>` -- the cubes of `d1 /\ d2`,
since disjunction distributes over conjunction; contradictory pairings come
back as padding cubes, so a downstream consumer like `cube_weight` (weight
`0` for padding) already treats them as absent without extra filtering.
Everything here is `std::array`, not `std::vector`: every size (`VARS`,
`CUBES`, `CUBES*CUBES`) is known at compile time, so there's no case where
a runtime-resizable container is actually needed.

Both functions are templated over `Ctx` (any `emp::BooleanContext`), not
tied to a specific session -- that's what makes this a separately testable
unit. `src/tests/dnf_distribute_test.cpp` drives it through
`emp::ClearSession` (plaintext: no OT, no network, no garbling, single
process) and checks it against hand-computed cases.

The same `conjoin`/`conjoin_dnf` code drops into the real 2PC circuit
unchanged: build `CircuitCube<SH2PCSession::ctx_t, N>` values via
`sess.input<BitVec_T<...>>(ALICE/BOB, ...)` as in `src/main.cpp`, run them
through these functions, and `sess.reveal(...)` the results.

### Cube weight

`src/gadgets/cube_weight.h` computes a cube's *weight*: the number of full
assignments over `VARS` (`=N`) variables that satisfy it. A cube fixes
`r = popcount(mask)` variables and leaves the other `VARS-r` free, so it's
satisfied by exactly `2^(VARS-r)` of the `2^VARS` possible assignments --
the standard cube-weight formula for counting a DNF's satisfying
assignments from its (disjoint) cubes. `cube_weight(c)` computes this for
one `CircuitCube`, returning a `UInt_T<Ctx, N+1>` (`N+1` bits is exactly
enough to hold the largest possible weight, `2^N`, without overflow);
`cube_weights<Ctx,N,M>(cubes)` maps it over a `std::array<CircuitCube<Ctx,N>,
M>` -- `M` is generic, so it works equally on `CUBES` cubes straight from a
parsed DNF or `CUBES*CUBES` results from `conjoin_dnf`.

A padding cube (`c.pad`) always gets weight `0`, regardless of its
`bits`/`mask` -- without that override, `mask=0^N` would otherwise give it
weight `2^N`, same as a genuine empty cube, which is wrong: a padding cube
isn't a real disjunct and must not contribute to a sum of weights across a
DNF.

Also tested via `emp::ClearSession` in `src/tests/cube_weight_test.cpp`.

### DNF weight

`src/gadgets/dnf_weight.h` sums an array of `CubeWeight<Ctx,N>` terms into a
single total -- the whole DNF's satisfying-assignment count, provided its
cubes are pairwise disjoint (true of a correctly-built cube cover; padding
cubes contribute `0` via `cube_weight`, so they don't skew the sum either).
`dnf_weight<Ctx,N,M>(weights)` takes a `std::array<CubeWeight<Ctx,N>, M>`
and returns a `DnfWeight<Ctx,N,M>` (`= UInt_T<Ctx, N + bits_for(M)>`) --
wide enough for the worst case, `M` terms each maxed out at `2^N`, since
`M * 2^N < 2^(N + bits_for(M))`. `bits_for` is `emp::kernel::bits_for`
(depends only on `M`, not on computing `2^N` directly, so it stays correct
even for large `N`).

Also tested via `emp::ClearSession` in `src/tests/dnf_weight_test.cpp`,
including summing the exact four cubes from `cube_weight_test.cpp`
(`16 + 1 + 8 + 0 = 25`).

### Cube intervals

`src/gadgets/cube_intervals.h` takes the same `std::array<CubeWeight<Ctx,N>,
M>` and computes its `M`-element *exclusive prefix sum*:
`intervals[0] = 0`, `intervals[i+1] = intervals[i] + weights[i]`.
`intervals[i]` is the total weight of every cube before `i` -- the starting
offset of cube `i`'s own block in a running enumeration of satisfying
assignments (useful for, e.g., mapping a random index into which cube it
falls in). The output stays length `M`, not `M+1`: `weights[M-1]` never
gets added to anything here, since there's no `intervals[M]` slot for that
sum -- `dnf_weight` computes the grand total separately, if that's what's
needed. Reuses `dnf_weight.h`'s `DnfWeight<Ctx,N,M>` as the element type,
since an exclusive prefix sum is a subset of the same summation.

Also tested via `emp::ClearSession` in `src/tests/cube_intervals_test.cpp`,
including the exact cube_weight/dnf_weight test cases: weights `[16,1,8,0]`
-> intervals `[0,16,17,25]`, and weights `[4,8,1,1]` -> intervals
`[0,4,12,13]`.

## Tests

Every `tests/*_test.cpp` file builds into one binary, `build/run_tests`:
each exposes a `run_*_tests()` function (its former `main()`) that
`tests/run_tests.cpp`'s actual `main()` calls in turn, instead of each test
file getting its own CMake executable target. A suite `exit(1)`s immediately
on its first failed check, so reaching the next suite already means the
previous one passed in full.

```sh
./build/run_tests
# === dimacs_dnf_test ===
# PASS basic: cube0 bits (x1 /\ ~x3 /\ x5)
# ...
# dimacs_dnf_test: all checks passed
# === dnf_distribute_test ===
# PASS disjoint variables
# ...
# dnf_distribute_test: all checks passed
# === cube_weight_test ===
# PASS empty cube: weight = 2^VARS
# ...
# cube_weight_test: all checks passed
# === dnf_weight_test ===
# PASS single cube: sum = its own weight
# ...
# dnf_weight_test: all checks passed
# === cube_intervals_test ===
# PASS empty+full+single+padding: intervals = [0,16,17,25]
# ...
# cube_intervals_test: all checks passed
# === all test suites passed ===
```

Adding a new test file means: write `tests/new_thing_test.cpp` with a
`run_new_thing_tests()` instead of `main()`, add it to `run_tests`'s source
list in `CMakeLists.txt`, and declare + call it from `run_tests.cpp`.

One trade-off worth knowing: `dimacs_dnf_test.cpp` itself stays pure stdlib
(no emp-tool, matching the parser it tests), but because it's linked into
the same `run_tests` binary as `dnf_distribute_test.cpp`, that one binary as
a whole now needs emp-tool too.

## Layout

- `CMakeLists.txt` -- finds/links `emp-tool`, `emp-ot`, `emp-sh2pc`, OpenSSL, GMP.
- `src/main.cpp` -- the two-party AND/OR/XOR/NOT/equality demo.
- `src/utils/` -- data-prep helpers with no emp-tool dependency:
  - `dimacs_dnf.h` -- the DIMACS-DNF cube parser (header-only: `parse<VARS,CUBES>` is a template).
- `src/gadgets/` -- Ctx-generic circuit gadgets, reusable across sessions:
  - `circuit_cube.h` -- `CircuitCube` and `CubeWeight`, the shared wire-level types.
  - `dnf_distribute.h` -- the DNF-cube conjunction gadget.
  - `cube_weight.h` -- the per-cube satisfying-assignment-count gadget.
  - `dnf_weight.h` -- sums cube weights into the whole DNF's satisfying-assignment count.
  - `cube_intervals.h` -- the exclusive prefix sum of cube weights.
- `src/tests/` -- unit tests, all built into the single `run_tests` binary:
  - `run_tests.cpp` -- the shared `main()`; calls each suite below.
  - `dimacs_dnf_test.cpp` -- tests for `utils/dimacs_dnf.h`.
  - `dnf_distribute_test.cpp` -- tests for `gadgets/dnf_distribute.h` (`emp::ClearSession`-based, no network).
  - `cube_weight_test.cpp` -- tests for `gadgets/cube_weight.h` (`emp::ClearSession`-based, no network).
  - `dnf_weight_test.cpp` -- tests for `gadgets/dnf_weight.h` (`emp::ClearSession`-based, no network).
  - `cube_intervals_test.cpp` -- tests for `gadgets/cube_intervals.h` (`emp::ClearSession`-based, no network).
- `sample.dnf.cnf` -- a standalone example DIMACS-DNF file.
- `run.sh` -- launches both parties locally for a quick check.
