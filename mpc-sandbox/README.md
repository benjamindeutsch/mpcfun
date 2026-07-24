# mpc-sandbox

A two-party garbled-circuit pipeline for DNF intersection counting, built on
[emp-toolkit](https://github.com/emp-toolkit) (`emp-tool` + `emp-ot` +
`emp-sh2pc`, semi-honest 2PC). Alice and Bob each hold a private DNF formula
(as a DIMACS-style file); the circuit computes the cubes of the two DNFs'
conjunction, each cube's satisfying-assignment count, both the total count
and the per-cube exclusive prefix sum over those counts, and then runs `K`
independent trials of a **Karp-Luby estimator** (joint-random cube
selection, a random satisfying assignment of it, a count of how many
conjunction cubes that assignment satisfies, and an oblivious 1/count
lookup) to approximate the *true* satisfying-assignment count of the
conjunction -- which can be less than the total from the disjoint-cube sum,
since the conjunction's cubes aren't guaranteed pairwise disjoint. Only
that final estimate is revealed to both parties; everything else (the
total weight, the interval boundaries, every trial's sample/selected
cube/assignment/satisfied count) stays private (see "Putting it together"
below for what's still a placeholder beyond that).

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
# [bob]   file=./bob.dnf  karp_luby_estimate_raw=180  (K=3, scale=12)  karp_luby_estimate=5
# [alice] file=./alice.dnf  karp_luby_estimate_raw=180  (K=3, scale=12)  karp_luby_estimate=5
```

(`karp_luby_estimate_raw` is the only revealed value -- `total_weight * sum`
of the `K` trials' `divide_lookup` reciprocals (see "Divide lookup" and
"Karp-Luby estimate" below), all `K` trials' sampling, selection, and
counting having stayed entirely private. Dividing that raw value by `K *
scale` in plaintext (free, since both are public compile-time constants)
gives the actual Karp-Luby estimate of the conjunction's true
satisfying-assignment count -- `5` here, below `total_weight=20`, since
these particular conjunction cubes overlap. Both parties always agree on
it, but it varies run to run, since it depends on `K` fresh joint-random
samples each time.)

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
`gadgets/dnf/dnf_distribute.h` (see its file for the fuller explanation).

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
(`emp::BitVec_T<Ctx,N>`/`emp::Bit_T<Ctx>`) instead of `vector<bool>`/`bool`;
`CubeData<Ctx,N>{bits, mask}` (no `pad`); and the `CubeWeight<Ctx,N>` and
`DnfWeight<Ctx,N,M>` aliases. All four are shared foundational types, used
by more than one gadget (`cube_weight.h`/`dnf_weight.h`/
`cube_intervals.h`/`select_cube.h`/`random_assignment.h`/
`karp_luby_estimate.h`) -- they live in their own file (rather than inside
whichever gadget happened to need them first) for exactly that reason.
`src/gadgets/common.h` is the analogous shared file for boilerplate rather
than types: the `using` declarations (`BitVec_T`, `Bit_T`, `array`, etc.)
every gadget needs, plus `zext_to<W>(v)` and `indicator<Count>(ctx, cond)`
-- two small helpers factored out of patterns that used to be reimplemented
in multiple gadgets (see `dnf/dnf_weight.h`, `dnf/cube_intervals.h`, and
`karp_luby/karp_luby_estimate.h` for `zext_to`; `karp_luby/select_cube.h`'s
`select_cube_index` and `karp_luby/count_satisfied_cubes.h` for
`indicator`).

`src/gadgets/dnf/dnf_distribute.h`'s `conjoin`/`conjoin_dnf` use `CircuitCube`
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
unit. `src/tests/dnf/dnf_distribute_test.cpp` drives it through
`emp::ClearSession` (plaintext: no OT, no network, no garbling, single
process) and checks it against hand-computed cases.

The same `conjoin`/`conjoin_dnf` code drops into the real 2PC circuit
unchanged: build `CircuitCube<SH2PCSession::ctx_t, N>` values via
`sess.input<BitVec_T<...>>(ALICE/BOB, ...)` as in `src/main.cpp`, run them
through these functions, and `sess.reveal(...)` the results.

### Cube weight

`src/gadgets/dnf/cube_weight.h` computes a cube's *weight*: the number of full
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

Also tested via `emp::ClearSession` in `src/tests/dnf/cube_weight_test.cpp`.

### DNF weight

`src/gadgets/dnf/dnf_weight.h` sums an array of `CubeWeight<Ctx,N>` terms into a
single total -- the whole DNF's satisfying-assignment count, provided its
cubes are pairwise disjoint (true of a correctly-built cube cover; padding
cubes contribute `0` via `cube_weight`, so they don't skew the sum either).
`dnf_weight<Ctx,N,M>(weights)` takes a `std::array<CubeWeight<Ctx,N>, M>`
and returns a `DnfWeight<Ctx,N,M>` (`= UInt_T<Ctx, N + bits_for(M)>`) --
wide enough for the worst case, `M` terms each maxed out at `2^N`, since
`M * 2^N < 2^(N + bits_for(M))`. `bits_for` is `emp::kernel::bits_for`
(depends only on `M`, not on computing `2^N` directly, so it stays correct
even for large `N`).

Also tested via `emp::ClearSession` in `src/tests/dnf/dnf_weight_test.cpp`,
including summing the exact four cubes from `cube_weight_test.cpp`
(`16 + 1 + 8 + 0 = 25`).

### Cube intervals

`src/gadgets/dnf/cube_intervals.h` takes the same `std::array<CubeWeight<Ctx,N>,
M>` and computes the `M+1` interval boundaries `T_0..T_M`: `T_0 = 0`,
`T_{i+1} = T_i + weights[i]`. `T_i` is the total weight of every cube
before `i` -- the starting offset of cube `i`'s own block in a running
enumeration of satisfying assignments (useful for, e.g., mapping a random
index into which cube it falls in). `T_M`, the last boundary, is the total
weight of all `M` cubes -- exactly `dnf_weight`'s result. Reuses
`circuit_cube.h`'s `DnfWeight<Ctx,N,M>` as the element type (same as
`dnf_weight`'s own return type), since this is a superset of the same
summation.

Also tested via `emp::ClearSession` in `src/tests/dnf/cube_intervals_test.cpp`,
including the exact cube_weight/dnf_weight test cases: weights `[16,1,8,0]`
-> intervals `[0,16,17,25,25]`, and weights `[4,8,1,1]` -> intervals
`[0,4,12,13,14]` (both `T_M` values matching the corresponding
`dnf_weight_test.cpp` totals: `25` and `14`).

### Select cube

`src/gadgets/karp_luby/select_cube.h` samples a joint random integer in
`[1, total_weight]`, finds which cube's block it falls in, and looks up
that cube's bits/mask -- composed from three separately testable pieces
(`select_cube(alice_r, bob_r, total, intervals, cubes)`, as `main.cpp`
calls it, just calls them in sequence and returns the final
`CubeData<Ctx,N>{bits, mask}`; the intermediate sample/index aren't part
of its return value -- a caller that wants those too, e.g. for testing,
calls `sample_in_range`/`select_cube_index` directly instead, as
`src/tests/karp_luby/select_cube_test.cpp` does):

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

Also tested via `emp::ClearSession` in `src/tests/karp_luby/select_cube_test.cpp`:
for `sample_in_range`, a basic case, wrapping above `total`, the
minimum/maximum possible sample, and both contributions nonzero; for
`select_cube_index`, every boundary (including exact hits, like `z=12`
landing in the cube ending at `intervals[2]=12`) against the exact
`intervals=[0,8,12,16,20]` from the `main.cpp` demo; for `cube_at_index`,
every index `1..4` against 4 distinct one-hot test cubes; and for the
composed `select_cube`, one case checking that a known `(sample, index)`
pair resolves to the right `cube.bits`/`cube.mask`.

### Random assignment

`src/gadgets/karp_luby/random_assignment.h` extends a `CubeData<Ctx,N>` (e.g. from
`select_cube`) into a full, uniformly random satisfying assignment over
all `N` variables:

```
assignment = bits | (~mask & r)
```

where `r = alice_r ^ bob_r` is a second joint random bitstring (same
free-XOR construction as `sample_in_range`). On a constrained variable
(`mask=1`): `bits | (~1 & r) = bits` -- the cube's own value wins. On a
free variable (`mask=0`, so `bits=0` there by the `circuit_cube.h`
convention): `bits | (~0 & r) = r` -- filled in randomly. So if `r` is
uniform, the result is a uniformly random assignment satisfying the cube.

Also tested via `emp::ClearSession` in
`src/tests/karp_luby/random_assignment_test.cpp`: a fully-constrained cube (the
assignment is just the cube's bits, `r` irrelevant), an empty cube (the
assignment is exactly `r`), and a partially-constrained cube with two
different `r` values (the fixed bit stays put; the free bits track `r`).

### Count satisfied cubes

`src/gadgets/karp_luby/count_satisfied_cubes.h` counts how many cubes in an array a
given assignment satisfies: cube `c` is satisfied iff `(assignment &
c.mask) == c.bits` -- assignment agrees with `c` on every variable `c`
constrains (variables `c` leaves free never affect the check). A padding
cube (`bits=1^N, mask=0^N`) is *never* satisfied by this check with no
special-casing needed: `assignment & 0^N = 0^N`, and `0^N != 1^N` for any
`N > 0`, so the equality always fails. `count_satisfied_cubes(assignment,
cubes)` returns a `SatisfiedCount<Ctx,M>` (`= UInt_T<Ctx, bits_for(M)>`),
built the same way as `select_cube_index`'s count: each `Bit_T` comparison
turned into 0/1 via `.select()` before accumulating.

Also tested via `emp::ClearSession` in
`src/tests/karp_luby/count_satisfied_cubes_test.cpp`, against 3 ordinary cubes plus
a padding cube: assignments satisfying `0`, `1`, `2`, and `3` of the
ordinary cubes, checking in each case that the padding cube is never
counted -- including when every bit of the assignment happens to match
the padding cube's own `bits` pattern.

### Divide lookup

`src/gadgets/karp_luby/divide_lookup.h` computes `1/count` for a `count` in
`1..M`, via an oblivious lookup table instead of a division circuit:
`count` is bounded by the small compile-time constant `M` (the number of
conjunction cubes), so a linear-scan mux over `M` precomputed constants
is cheaper than an actual divider circuit. emp-toolkit has no native
lookup-table/lookup-argument primitive for garbled circuits (that term
elsewhere refers to SNARK/polynomial-commitment techniques like Plookup,
which don't apply here) -- so this is the same equality-compare +
`.select()` mux chain as `select_cube_index`/`count_satisfied_cubes`,
just keyed by `count` instead of accumulating a sum.

Since division isn't generally exact, the table stores a *fixed-point*
reciprocal: `lookup_scale<M>() = lcm(1..M)` (computed at compile time via
`constexpr` `gcd`/`lcm`), and `divide_lookup(count)` returns `scale /
count` -- exact, no rounding, since `scale` is a multiple of every `count`
in range by construction. A caller divides the *revealed* result by
`scale` in plaintext to recover the true `1/count`; `scale` is a public
compile-time constant, so that division is free (no circuit gates spent
on it). `DivideLookupResult<Ctx,M>` (`= UInt_T<Ctx, bits_for(scale)>`) is
wide enough to hold the largest table entry, `scale` itself (`count=1`).

Also tested via `emp::ClearSession` in `src/tests/karp_luby/divide_lookup_test.cpp`:
`M=4` gives `scale=12` (`lcm(1,2,3,4)`), and checks `divide_lookup(1..4)
== 12, 6, 4, 3`.

### Karp-Luby estimate

`src/gadgets/karp_luby/karp_luby_estimate.h` computes `dnf_weight * sum_t
reciprocals[t]` over `K` independent trials -- the raw (unnormalized)
numerator of the [Karp-Luby estimator](https://en.wikipedia.org/wiki/Karp%E2%80%93Luby_algorithm)
(a.k.a. the "coverage algorithm") for the *true* number of satisfying
assignments of a union of possibly-overlapping sets -- here, the
conjunction's cubes, which (unlike a correctly-built disjoint cube cover)
aren't guaranteed pairwise disjoint, so `dnf_weight`'s plain sum
over-counts assignments satisfying more than one cube.

Each trial is one independent run of `select_cube -> random_assignment ->
count_satisfied_cubes -> divide_lookup` (see their sections above): sample
a cube weighted by its size, sample a uniform satisfying assignment of it,
count how many conjunction cubes that assignment satisfies (`count`), and
look up `1/count`. This makes `E[1/count] = true_count / dnf_weight` (the
standard Karp-Luby argument), so `E[dnf_weight * sum_t(1/count_t)] = K *
true_count`. `karp_luby_estimate<Ctx,N,M,K>(weight, reciprocals)` takes
the `DnfWeight<Ctx,N,M>` total and a `std::array<DivideLookupResult<Ctx,M>,
K>` of the `K` trials' reciprocals, and returns the raw product as a
`KarpLubyEstimate<Ctx,N,M,K>` -- wide enough for the worst case via the
usual zext-to-common-width-then-multiply/sum pattern.

The actual unbiased estimate is `(this result) / (K *
lookup_scale<M>())`: both `K` and the lookup scale are public compile-time
constants, so a caller does that division for free in plaintext on the
*revealed* result, same as `divide_lookup`'s own un-scaling.

Also tested via `emp::ClearSession` in
`src/tests/karp_luby/karp_luby_estimate_test.cpp`, `N=4,M=4,K=3`: e.g.
`weight=20, reciprocals=[12,6,3] -> 20*(12+6+3) = 420` (matching
`main.cpp`'s real `dnf_weight=20`, and 3 trials with `count = 1, 2, 4`),
and a degenerate `weight=0 -> estimate=0` regardless of the reciprocals.

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
   (`cube_intervals`), then
6. runs `K` independent trials (`K=3` currently) of the Karp-Luby
   sub-pipeline, each with its own fresh joint-random draws, all of it
   staying private (nothing about any individual trial is revealed):
   1. draws each party's local random contribution and, from it, samples a
      joint uniform value in `[1, total_weight]`, finds which cube's block
      it falls in, and looks up that cube's bits/mask (`select_cube` --
      see the "Select cube" section above),
   2. draws a second joint random contribution and extends the selected
      cube into a full random satisfying assignment over all `VARS`
      variables (`random_assignment` -- see its section above),
   3. counts how many of the `CUBES*CUBES` conjunction cubes that
      assignment actually satisfies (`count_satisfied_cubes` -- see its
      section above): always `>= 1` (the cube it was built from), possibly
      more, since these particular conjunction cubes aren't guaranteed
      pairwise disjoint, and
   4. looks up that count's fixed-point reciprocal (`divide_lookup` -- see
      its section above), and
7. sums the `K` reciprocals and multiplies by the total from step 4
   (`karp_luby_estimate` -- see its section above), the raw numerator of
   the Karp-Luby estimate of the conjunction's *true* satisfying-assignment
   count.

`VARS`/`CUBES` (currently `4`/`2`, so `PRODUCT = CUBES*CUBES = 4`) are
compile-time constants shared by both parties, not read from either file --
the circuit's size has to be public in MPC, which is the whole reason
`dimacs_dnf::parse` pads to a fixed capacity instead of just sizing to
whatever's in the file.

Only step 7's final raw estimate is revealed to both parties -- the total
weight, the interval boundaries, and every trial's sample/selected
cube/assignment/satisfied count all stay private, known to neither party.
That's not quite the final protocol either -- a real deployment would
likely want to keep even the estimate private (e.g. only reveal a
threshold check against it) -- but it's the actual Karp-Luby computation,
not a placeholder.

`alice.dnf` / `bob.dnf` at the project root are the bundled example
inputs (`VARS=4, CUBES=2`, matching `main.cpp`): Alice's DNF is `x1 ∨ ¬x2`,
Bob's is `x1 ∨ x3`. Their conjunction's 4 cubes all turn out non-contradictory
(weights `8, 4, 4, 4` in cross-product order), giving a (private)
`total_weight=20` -- which exceeds `2^VARS=16`, the maximum possible number
of distinct assignments, so these 4 conjunction cubes necessarily overlap.
The revealed `karp_luby_estimate` (e.g. `5` in the "Run" example above,
varying run to run since it depends on `K` fresh joint-random samples each
time) comes out below `total_weight=20`, correctly reflecting that overlap.

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
# === select_cube_test ===
# ...
# === random_assignment_test ===
# ...
# === count_satisfied_cubes_test ===
# ...
# === divide_lookup_test ===
# ...
# === karp_luby_estimate_test ===
# ...
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
  - `circuit_cube.h` -- `CircuitCube`, `CubeData`, `CubeWeight`, `DnfWeight`: the shared wire-level types.
  - `common.h` -- the `using` declarations every gadget below needs, plus two shared helpers (`zext_to`, `indicator`) that replace boilerplate several gadgets used to each reimplement.
  - `dnf/` -- the DNF-conjunction-and-weighting stage:
    - `dnf_distribute.h` -- the DNF-cube conjunction gadget.
    - `cube_weight.h` -- the per-cube satisfying-assignment-count gadget.
    - `dnf_weight.h` -- sums cube weights into the whole DNF's satisfying-assignment count.
    - `cube_intervals.h` -- the exclusive prefix sum of cube weights.
  - `karp_luby/` -- the Karp-Luby estimation stage:
    - `select_cube.h` -- samples a joint random value, then selects and looks up the cube it lands in.
    - `random_assignment.h` -- extends a selected cube into a full random satisfying assignment.
    - `count_satisfied_cubes.h` -- counts how many cubes in an array an assignment satisfies.
    - `divide_lookup.h` -- oblivious fixed-point `1/count` lookup table.
    - `karp_luby_estimate.h` -- combines `K` trials' reciprocals and the total weight into the raw Karp-Luby estimate.
- `src/tests/` -- unit tests, all built into the single `run_tests` binary:
  - `run_tests.cpp` -- the shared `main()`; calls each suite below.
  - `bits_of.h` -- pure-stdlib `bits_of<N>(s)` helper (a `'0'`/`'1'` string -> `std::array<bool,N>`), shared by every test file including `dimacs_dnf_test.cpp`.
  - `test_helpers.h` -- emp-tool dependent helpers built on `bits_of.h`: `bits_of_uint<WIDTH>` and `make_cube<Session,Ctx,N>`, shared by every gadget test file.
  - `dimacs_dnf_test.cpp` -- tests for `utils/dimacs_dnf.h`.
  - `dnf/` -- tests for `gadgets/dnf/`:
    - `dnf_distribute_test.cpp`, `cube_weight_test.cpp`, `dnf_weight_test.cpp`, `cube_intervals_test.cpp` (all `emp::ClearSession`-based, no network).
  - `karp_luby/` -- tests for `gadgets/karp_luby/`:
    - `select_cube_test.cpp`, `random_assignment_test.cpp`, `count_satisfied_cubes_test.cpp`, `divide_lookup_test.cpp`, `karp_luby_estimate_test.cpp` (all `emp::ClearSession`-based, no network).
- `sample.dnf` -- a standalone example DIMACS-DNF file (used by `dimacs_dnf_test.cpp`'s docs, not read by any binary).
- `alice.dnf` / `bob.dnf` -- the two parties' example private inputs to `main.cpp`.
- `run.sh` -- launches both parties of `sh2pc_demo` locally for a quick check.
