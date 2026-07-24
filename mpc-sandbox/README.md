# mpc-sandbox

A two-party garbled-circuit pipeline for DNF intersection counting, built on
[emp-toolkit](https://github.com/emp-toolkit) (`emp-tool` + `emp-ot` +
`emp-sh2pc`, semi-honest 2PC). Alice and Bob each hold a private DNF formula
(as a DIMACS-style file); the circuit computes the cubes of the two DNFs'
conjunction, each cube's satisfying-assignment count, both the total count
and the per-cube exclusive prefix sum over those counts, and a joint
uniform random sample in `[1, total_weight]` -- currently revealing all of
that to both parties, purely so the pipeline can be smoke-tested end to end
(see "Putting it together" below for what that means and what's still a
placeholder).

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
./build/sh2pc_demo <party: 1=ALICE, 2=BOB> <path-to-dimacs-dnf-file>
```

The file is each party's own private DNF (see "DIMACS-DNF cube parser"
below for the format) -- it must fit `main.cpp`'s compile-time `VARS`/
`CUBES` capacity (currently `VARS=4, CUBES=2`).

Two terminals:

```sh
# terminal 1
./build/sh2pc_demo 1 alice.dnf

# terminal 2
./build/sh2pc_demo 2 bob.dnf
```

Or both at once via the helper script (`./run.sh <alice-file> <bob-file>`,
both args optional -- default to the bundled `alice.dnf`/`bob.dnf`):

```sh
./run.sh
# [alice] file=alice.dnf  total_weight=20  sample=15  cube_index(1-indexed)=3  cube_bits=1000  cube_mask=1100  intervals=[0,8,12,16,20]
# [bob]   file=bob.dnf  total_weight=20  sample=15  cube_index(1-indexed)=3  cube_bits=1000  cube_mask=1100  intervals=[0,8,12,16,20]
```

(`sample` is a fresh joint-random draw in `[1, total_weight]` each run;
`cube_index` is the (1-indexed) cube whose block it falls in, and
`cube_bits`/`cube_mask` are that cube's data -- both parties always agree
on all of it, but it varies run to run.)

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

(`sample.dnf` at the project root is a standalone example of the file
format -- 5 vars / 3 cubes: cube 0 is `x1 ∧ ¬x3 ∧ x5`, cube 1 is `¬x2 ∧ x4`,
cube 2 is the empty cube, always true. The test above embeds the same
content inline rather than reading that file, so it isn't tied to a
particular working directory.)

## Circuit gadgets

`src/gadgets/circuit_cube.h` defines `CircuitCube<Ctx,N>{bits, mask, pad}`,
the same fields as `dimacs_dnf::Cube` but as wires
(`emp::BitVec_T<Ctx,N>`/`emp::Bit_T<Ctx>`) instead of `vector<bool>`/`bool`,
plus the `CubeWeight<Ctx,N>` and `DnfWeight<Ctx,N,M>` aliases used by
`cube_weight.h`/`dnf_weight.h`/`cube_intervals.h`/`select_cube.h`. All
three are shared foundational types, not specific to any one gadget --
they live in their own file (rather than inside whichever gadget happened
to need them first) for exactly that reason.

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
M>` and computes the `M+1` interval boundaries `T_0..T_M`: `T_0 = 0`,
`T_{i+1} = T_i + weights[i]`. `T_i` is the total weight of every cube
before `i` -- the starting offset of cube `i`'s own block in a running
enumeration of satisfying assignments (useful for, e.g., mapping a random
index into which cube it falls in). `T_M`, the last boundary, is the total
weight of all `M` cubes -- exactly `dnf_weight`'s result. Reuses
`circuit_cube.h`'s `DnfWeight<Ctx,N,M>` as the element type (same as
`dnf_weight`'s own return type), since this is a superset of the same
summation.

Also tested via `emp::ClearSession` in `src/tests/cube_intervals_test.cpp`,
including the exact cube_weight/dnf_weight test cases: weights `[16,1,8,0]`
-> intervals `[0,16,17,25,25]`, and weights `[4,8,1,1]` -> intervals
`[0,4,12,13,14]` (both `T_M` values matching the corresponding
`dnf_weight_test.cpp` totals: `25` and `14`).

### Select cube

`src/gadgets/select_cube.h` samples a joint random integer in
`[1, total_weight]`, finds which cube's block it falls in, and looks up
that cube's bits/mask -- composed from three separately testable pieces
(`select_cube(alice_r, bob_r, total, intervals, cubes)` just calls them in
sequence and returns the final `CubeData<Ctx,N>{bits, mask}`; the
intermediate sample/index aren't part of its return value -- a caller that
wants those too, e.g. for testing, calls `sample_in_range`/
`select_cube_index` directly instead, as `main.cpp` does):

1. **`sample_in_range(alice_r, bob_r, total)`** computes `(alice_r ^
   bob_r).as_uint() % total`, then `+1`. The `^` is a free-XOR coin flip --
   uniform as long as *either* contribution is honestly random, regardless
   of what the other party supplies -- so this only does that wire-level
   combination; drawing the actual random bits (real entropy, not a fixed
   seed) and feeding them in as private input is the caller's job, same as
   any other private input. Assumes `total > 0` (the conjunction has at
   least one satisfiable cube) -- mod-by-zero otherwise. `SampleBits<Ctx,
   N,M>` is the contribution type -- a `BitVec_T` matching
   `DnfWeight<Ctx,N,M>`'s own width, so the XOR result reinterprets as one
   for free.

2. **`select_cube_index(z, intervals)`** finds the unique **1-indexed**
   `j` with `intervals[j-1] < z <= intervals[j]` -- i.e. which cube's
   block the sample `z` landed in, counting cubes from `1` (cube `1` is
   `intervals[0..1]`, cube `M` is `intervals[M-1..M]`). Computed as `j =
   sum_{i=0}^{M} [z > intervals[i]]`: `intervals` is non-decreasing, so
   the boundaries below `z` are exactly `intervals[0..j-1]` (`j` of them),
   making the raw sum equal to `j` directly -- no separate "invert the
   count" step needed. Each `[z > intervals[i]]` comparison is a `Bit_T`,
   turned into a 0/1 `CubeIndex<Ctx,M>` (`= UInt_T<Ctx, bits_for(M+1)>`)
   via `.select()` (`sel ? 1 : 0`) before accumulating.

3. **`cube_at_index(index, cubes)`** obliviously looks up
   `cubes[index-1]`'s bits/mask (translating the 1-indexed `index` back to
   a 0-indexed array position happens for free, by comparing `index`
   against the constant `i+1` for each candidate `i`, rather than via a
   separate subtraction). A linear-scan mux: `O(M)` equality comparisons
   and `.select()`s, fine for the small `M` this pipeline uses. Returns a
   `CubeData<Ctx,N>{bits, mask}` -- no `pad`, since a validly-sampled
   index always lands in a non-padding cube (a padding cube has weight
   `0`, so it occupies a zero-width slice of the intervals that `z` can
   never land in).

Also tested via `emp::ClearSession` in `src/tests/select_cube_test.cpp`:
for `sample_in_range`, a basic case, wrapping above `total`, the
minimum/maximum possible sample, and both contributions nonzero; for
`select_cube_index`, every boundary (including exact hits, like `z=12`
landing in the cube ending at `intervals[2]=12`) against the exact
`intervals=[0,8,12,16,20]` from the `main.cpp` demo; for `cube_at_index`,
every index `1..4` against 4 distinct one-hot test cubes; and for the
composed `select_cube`, one case checking that a known `(sample, index)`
pair resolves to the right `cube.bits`/`cube.mask`.

## Putting it together

`src/main.cpp` is the two-party pipeline: each party's private input is a
path to its own DIMACS-DNF file. Parsing (`dimacs_dnf::parse<VARS,CUBES>`)
happens locally, in the clear, before either party touches the network --
it's data prep, not a circuit. The parsed cubes then become each party's
private input to the circuit, which:

1. builds `CircuitCube<Ctx,VARS>` inputs for both parties' `CUBES` cubes,
2. computes the `CUBES*CUBES` cubes of their conjunction (`conjoin_dnf`),
3. computes each result's weight (`cube_weights`),
4. sums those into the conjunction's total satisfying-assignment count
   (`dnf_weight`),
5. computes the `CUBES*CUBES+1` interval boundaries over the weights
   (`cube_intervals`), and
6. draws each party's local random contribution and, from it, samples a
   joint uniform value in `[1, total_weight]`, finds which cube's block it
   falls in, and looks up that cube's bits/mask (see the "Select cube"
   section above). `main.cpp` calls the three underlying pieces
   (`sample_in_range`/`select_cube_index`/`cube_at_index`) directly rather
   than the composed `select_cube`, since it also wants to reveal the
   intermediate sample/index, which `select_cube` doesn't return.

`VARS`/`CUBES` (currently `4`/`2`, so `PRODUCT = CUBES*CUBES = 4`) are
compile-time constants shared by both parties, not read from either file --
the circuit's size has to be public in MPC, which is the whole reason
`dimacs_dnf::parse` pads to a fixed capacity instead of just sizing to
whatever's in the file.

Everything above is revealed to both parties at the end. **That's not the
final protocol** -- it's a placeholder so the whole pipeline can be
smoke-tested end to end. A real circuit would consume all of it
*privately* (e.g. a threshold check on the total, or actually using the
selected cube's bits/mask for something) instead of leaking it; expand
`main.cpp` once that's built.

`alice.dnf` / `bob.dnf` at the project root are the bundled example
inputs (`VARS=4, CUBES=2`, matching `main.cpp`): Alice's DNF is `x1 ∨ ¬x2`,
Bob's is `x1 ∨ x3`. Their conjunction's 4 cubes all turn out non-contradictory
here (weights `8, 4, 4, 4` in cross-product order), giving `total_weight=20`
and `intervals=[0,8,12,16,20]` (5 boundaries for 4 cubes; the last one, `20`,
is the total weight). `sample` is a fresh draw in `[1,20]` every run, and
`cube_index` (1-indexed, in `[1,4]`) is whichever of the 4 cubes its block
landed in -- e.g. `sample=15` lands in cube `3` (`x1 ∧ ¬x2`, from `A1 ∧
B0`), reported as `cube_bits=1000, cube_mask=1100`.

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
- `src/main.cpp` -- the two-party DNF-intersection-counting pipeline (see "Putting it together").
- `src/utils/` -- data-prep helpers with no emp-tool dependency:
  - `dimacs_dnf.h` -- the DIMACS-DNF cube parser (header-only: `parse<VARS,CUBES>` is a template).
- `src/gadgets/` -- Ctx-generic circuit gadgets, reusable across sessions:
  - `circuit_cube.h` -- `CircuitCube`, `CubeWeight`, `DnfWeight`: the shared wire-level types.
  - `dnf_distribute.h` -- the DNF-cube conjunction gadget.
  - `cube_weight.h` -- the per-cube satisfying-assignment-count gadget.
  - `dnf_weight.h` -- sums cube weights into the whole DNF's satisfying-assignment count.
  - `cube_intervals.h` -- the exclusive prefix sum of cube weights.
  - `select_cube.h` -- samples a joint random value, then selects and looks up the cube it lands in.
- `src/tests/` -- unit tests, all built into the single `run_tests` binary:
  - `run_tests.cpp` -- the shared `main()`; calls each suite below.
  - `dimacs_dnf_test.cpp` -- tests for `utils/dimacs_dnf.h`.
  - `dnf_distribute_test.cpp` -- tests for `gadgets/dnf_distribute.h` (`emp::ClearSession`-based, no network).
  - `cube_weight_test.cpp` -- tests for `gadgets/cube_weight.h` (`emp::ClearSession`-based, no network).
  - `dnf_weight_test.cpp` -- tests for `gadgets/dnf_weight.h` (`emp::ClearSession`-based, no network).
  - `cube_intervals_test.cpp` -- tests for `gadgets/cube_intervals.h` (`emp::ClearSession`-based, no network).
  - `select_cube_test.cpp` -- tests for `gadgets/select_cube.h` (`emp::ClearSession`-based, no network).
- `sample.dnf` -- a standalone example DIMACS-DNF file (used by `dimacs_dnf_test.cpp`'s docs, not read by any binary).
- `alice.dnf` / `bob.dnf` -- the two parties' example private inputs to `main.cpp`.
- `run.sh` -- launches both parties of `sh2pc_demo` locally for a quick check.
