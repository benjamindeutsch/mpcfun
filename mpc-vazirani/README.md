# mpc-vazirani

A two-party garbled-circuit pipeline for DNF intersection counting, built on
[emp-toolkit](https://github.com/emp-toolkit) (`emp-tool` + `emp-ot` +
`emp-sh2pc`, semi-honest 2PC). Alice and Bob each hold a private DNF formula
(as a DIMACS-style file); the circuit computes the cubes of the two DNFs'
conjunction, each cube's satisfying-assignment count, both the total count
and the per-cube exclusive prefix sum over those counts, and then runs `K`
independent trials of a **Vazirani estimator** (joint-random cube
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
# [bob]   file=./bob.dnf  vazirani_estimate_raw=2621440  (K=3, scale=65536)  vazirani_estimate=13.3333
# [alice] file=./alice.dnf  vazirani_estimate_raw=2621440  (K=3, scale=65536)  vazirani_estimate=13.3333
```

(`vazirani_estimate_raw` is the only revealed value -- `total_weight * sum`
of the `K` trials' `divide_lookup` reciprocals (see "Divide lookup" and
"Vazirani estimate" below), all `K` trials' sampling, selection, and
counting having stayed entirely private. Dividing that raw value by `K *
scale` in plaintext (free, since both are public compile-time constants)
gives the actual Vazirani estimate of the conjunction's true
satisfying-assignment count -- `13.3` here, in the same ballpark as
`total_weight=20`, since these particular conjunction cubes overlap only
partially and `K=3` is far too few trials for a tight estimate (see
"Benchmarks" below for what a real `K` looks like). Both parties always
agree on it, but it varies run to run, since it depends on `K` fresh
joint-random samples each time.)

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
`vazirani_estimate.h`) -- they live in their own file (rather than inside
whichever gadget happened to need them first) for exactly that reason.
`src/gadgets/common.h` is the analogous shared file for boilerplate rather
than types: the `using` declarations (`BitVec_T`, `Bit_T`, `array`, etc.)
every gadget needs, plus `zext_to<W>(v)` and `indicator<Count>(ctx, cond)`
-- two small helpers factored out of patterns that used to be reimplemented
in multiple gadgets (see `dnf/dnf_weight.h`, `dnf/cube_intervals.h`, and
`vazirani/vazirani_estimate.h` for `zext_to`; `vazirani/select_cube.h`'s
`select_cube_index` and `vazirani/count_satisfied_cubes.h` for
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

`src/gadgets/vazirani/select_cube.h` samples a joint random integer in
`[1, total_weight]`, finds which cube's block it falls in, and looks up
that cube's bits/mask -- composed from three separately testable pieces
(`select_cube(alice_r, bob_r, total, intervals, cubes)`, as `main.cpp`
calls it, just calls them in sequence and returns the final
`CubeData<Ctx,N>{bits, mask}`; the intermediate sample/index aren't part
of its return value -- a caller that wants those too, e.g. for testing,
calls `sample_in_range`/`select_cube_index` directly instead, as
`src/tests/vazirani/select_cube_test.cpp` does):

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

Also tested via `emp::ClearSession` in `src/tests/vazirani/select_cube_test.cpp`:
for `sample_in_range`, a basic case, wrapping above `total`, the
minimum/maximum possible sample, and both contributions nonzero; for
`select_cube_index`, every boundary (including exact hits, like `z=12`
landing in the cube ending at `intervals[2]=12`) against the exact
`intervals=[0,8,12,16,20]` from the `main.cpp` demo; for `cube_at_index`,
every index `1..4` against 4 distinct one-hot test cubes; and for the
composed `select_cube`, one case checking that a known `(sample, index)`
pair resolves to the right `cube.bits`/`cube.mask`.

### Random assignment

`src/gadgets/vazirani/random_assignment.h` extends a `CubeData<Ctx,N>` (e.g. from
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
`src/tests/vazirani/random_assignment_test.cpp`: a fully-constrained cube (the
assignment is just the cube's bits, `r` irrelevant), an empty cube (the
assignment is exactly `r`), and a partially-constrained cube with two
different `r` values (the fixed bit stays put; the free bits track `r`).

### Count satisfied cubes

`src/gadgets/vazirani/count_satisfied_cubes.h` counts how many cubes in an array a
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
`src/tests/vazirani/count_satisfied_cubes_test.cpp`, against 3 ordinary cubes plus
a padding cube: assignments satisfying `0`, `1`, `2`, and `3` of the
ordinary cubes, checking in each case that the padding cube is never
counted -- including when every bit of the assignment happens to match
the padding cube's own `bits` pattern.

### Divide lookup

`src/gadgets/vazirani/divide_lookup.h` computes `1/count` for a `count` in
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
reciprocal: `lookup_scale<M>()` returns a **fixed** `SCALE = 2^16 =
65536`, the same for every `M`, and `divide_lookup(count)` returns
`round(scale / count)`. An earlier version instead used `scale =
lcm(1..M)` for *exact*, rounding-free reciprocals (a multiple of every
`count` in range by construction) -- fine at the small `M=4` this
pipeline first ran at, but it doesn't scale: by the prime number theorem,
`lcm(1..M)`'s bit-length grows roughly linearly in `M` (`lcm(1..42)`
alone already needs 58 bits), it overflows a `uint64_t` entirely around
`M=47`, and even computed exactly via a bignum, every downstream
reciprocal/sum/multiply would end up operating on thousands-of-bits-wide
wires once `M` reaches the low thousands (see "Benchmarks" below, where
`M` does) -- unusable. A fixed power-of-two `SCALE` keeps every table
entry's width constant regardless of `M`, at the cost of making
reciprocals approximate instead of exact: relative rounding error is at
most `2^-16` per entry, utterly negligible next to the Vazirani
estimator's own `O(1/sqrt(K))` sampling error at any `K` actually
affordable to run. A caller divides the *revealed* result by `scale` in
plaintext to recover the (approximate) `1/count`; `scale` is a public
compile-time constant, so that division is free (no circuit gates spent
on it). `DivideLookupResult<Ctx,M>` (`= UInt_T<Ctx, 17>`) is wide enough
to hold the largest table entry, `scale` itself (`count=1`, the one case
that needs no rounding) -- a fixed width, independent of `M`.

Also tested via `emp::ClearSession` in `src/tests/vazirani/divide_lookup_test.cpp`:
checks `divide_lookup(1..4) == 65536, 32768, 21845, 16384` (exact for
`1,2,4`, which divide `65536` evenly; rounded for `3`).

### Vazirani estimate

`src/gadgets/vazirani/vazirani_estimate.h` computes `dnf_weight * sum_t
reciprocals[t]` over `K` independent trials -- the raw (unnormalized)
numerator of the Vazirani estimator (a.k.a. the "coverage algorithm") for
the *true* number of satisfying
assignments of a union of possibly-overlapping sets -- here, the
conjunction's cubes, which (unlike a correctly-built disjoint cube cover)
aren't guaranteed pairwise disjoint, so `dnf_weight`'s plain sum
over-counts assignments satisfying more than one cube.

Each trial is one independent run of `select_cube -> random_assignment ->
count_satisfied_cubes -> divide_lookup` (see their sections above): sample
a cube weighted by its size, sample a uniform satisfying assignment of it,
count how many conjunction cubes that assignment satisfies (`count`), and
look up `1/count`. This makes `E[1/count] = true_count / dnf_weight` (the
standard Vazirani argument), so `E[dnf_weight * sum_t(1/count_t)] = K *
true_count`. `vazirani_estimate<Ctx,N,M,K>(weight, reciprocals)` takes
the `DnfWeight<Ctx,N,M>` total and a `std::array<DivideLookupResult<Ctx,M>,
K>` of the `K` trials' reciprocals, and returns the raw product as a
`VaziraniEstimate<Ctx,N,M,K>` -- wide enough for the worst case via the
usual zext-to-common-width-then-multiply/sum pattern. The intermediate
sum's width (`VaziraniSum<Ctx,M,K>`) is computed as `(kDivideLookupScaleBits
+ 1) + bits_for(K)` -- an *addition*, not `bits_for(K * lookup_scale<M>())`
-- since that product can exceed `INT_MAX` well before `K` itself does,
and `emp::kernel::bits_for` takes a plain `int`; the addition gives the
same bound (summing `K` terms each under `2^(kDivideLookupScaleBits+1)`)
without ever forming the oversized product just to truncate it.

The actual unbiased estimate is `(this result) / (K *
lookup_scale<M>())`: both `K` and the lookup scale are public compile-time
constants, so a caller does that division for free in plaintext on the
*revealed* result, same as `divide_lookup`'s own un-scaling.

Also tested via `emp::ClearSession` in
`src/tests/vazirani/vazirani_estimate_test.cpp`, `N=4,M=4,K=3`: e.g.
`weight=20, reciprocals=[65536,32768,16384] -> 20*114688 = 2293760`
(matching `main.cpp`'s real `dnf_weight=20`, and 3 trials with `count =
1, 2, 4`), and a degenerate `weight=0 -> estimate=0` regardless of the
reciprocals.

The same file also has `vazirani_trials` -- plain host-side `constexpr`
calculations (no `Ctx`/wires involved, unlike everything else in this
file), *not* circuit gadgets: the number of trials `K` needed for the
resulting estimate to be within relative error `epsilon` of the true
count with probability `>= 1-delta`. Two overloads:

- `vazirani_trials(double variance_ratio, double epsilon, double delta)`
  is the real bound:
  `K = ceil(min(1/delta, 3*ln(2/delta)) * variance_ratio / epsilon^2)`.
  `variance_ratio` is `Var(X_t)/E[X_t]^2` for a single trial's `X_t =
  1/count` -- with `X_t <= 1`, `Var(X_t) <= E[X_t] - E[X_t]^2`, so
  `Var/mu^2 <= 1/mu - 1 = S/|U| - 1` (`S` = `dnf_weight`, `|U|` = the true
  count), which is at most `S/W_max - 1 <= N - 1` (`N` = number of
  conjunction cubes, `W_max` = the largest one's weight). **This bound is
  linear in `N`, not quadratic -- that linearity is the actual Vazirani
  theorem.** `min(1/delta, 3*ln(2/delta))` takes the better of two
  concentration bounds: Chebyshev (`1/delta`, from the argument above) or
  a multiplicative Chernoff bound (`3*ln(2/delta)`, valid since the `X_t`
  are i.i.d. in `[0,1]`) -- Chernoff wins for `delta` below roughly `0.15`.
  (`const_log`, alongside it in the same file, is a `constexpr` natural
  log via `atanh`-series expansion, since `<cmath>`'s `std::log` isn't
  reliably usable in a constant expression.)
- `vazirani_trials(int cubes, double epsilon, double delta)` is the
  convenience form every caller actually uses: a conservative
  `variance_ratio = cubes^2 - 1`, taking `cubes` as the *per-party* cube
  count (so `cubes^2` is `N`, the conjoined DNF's cube count -- see
  "Putting it together" above) and using the worst case `W_max=1` (every
  cube fully constrained) rather than requiring the real weights at
  compile time.

An earlier version of this function was quadratic in `(vars^2-1)^2`
(mixing up `vars` -- the *variable* count -- with the cube count, and
missing the linear-not-quadratic bound above) -- see "Benchmarks" below
for how much that mistake cost. Callers pick `K` from this before
touching any of the `K`-templated gadgets above --
`src/bench/bench_vazirani.cpp` does exactly that, calling
`vazirani_trials(CUBES, epsilon, delta)`; `src/main.cpp` doesn't, since
its `K=3` is deliberately just a fast interactive smoke test, not a real
accuracy guarantee.

## Putting it together

`src/pipeline/vazirani_pipeline.h`'s `run_vazirani_pipeline<VARS,CUBES,K>
(sess, my_dnf)` is the two-party circuit itself -- each party's already
locally-`dimacs_dnf::parse<VARS,CUBES>`d DNF in (parsing happens before
either party touches the network -- it's data prep, not a circuit; see
"DIMACS-DNF cube parser" above), a single revealed raw Vazirani numerator
out. It's a function rather than living directly in `main()` because two
different binaries need it with two different `K`s: `src/main.cpp` (the
interactive demo, `K=3`, just a fast smoke test) and
`src/bench/bench_vazirani.cpp` (the benchmark, `K` from a real target
epsilon -- see "Benchmarks" below) would otherwise each carry their own
copy. Not `Ctx`-generic like the `gadgets/` headers it calls -- it's
`SH2PCSession`-only, since it does real network I/O via `sess.input`/
`sess.reveal`, unlike the pure wire-level gadgets underneath it, which stay
testable under `ClearSession`.

Given each party's already-parsed cubes as private input, the circuit:

1. builds `CircuitCube<Ctx,VARS>` inputs for both parties' `CUBES` cubes,
2. computes the `CUBES*CUBES` cubes of their conjunction (`conjoin_dnf`),
3. computes each result's weight (`cube_weights`),
4. sums those into the conjunction's total satisfying-assignment count
   (`dnf_weight`),
5. computes the `CUBES*CUBES+1` interval boundaries over the weights
   (`cube_intervals`), then
6. runs `K` independent trials (a template parameter -- see above for how
   the two callers each pick it) of the Vazirani sub-pipeline, each with
   its own fresh joint-random draws, all of it staying private (nothing
   about any individual trial is revealed):
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
   (`vazirani_estimate` -- see its section above), the raw numerator of
   the Vazirani estimate of the conjunction's *true* satisfying-assignment
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
threshold check against it) -- but it's the actual Vazirani computation,
not a placeholder.

`alice.dnf` / `bob.dnf` at the project root are the bundled example
inputs (`VARS=4, CUBES=2`, matching `main.cpp`): Alice's DNF is `x1 ∨ ¬x2`,
Bob's is `x1 ∨ x3`. Their conjunction's 4 cubes all turn out non-contradictory
(weights `8, 4, 4, 4` in cross-product order), giving a (private)
`total_weight=20` -- which exceeds `2^VARS=16`, the maximum possible number
of distinct assignments, so these 4 conjunction cubes necessarily overlap.
The revealed `vazirani_estimate` (e.g. `5` in the "Run" example above,
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
# === vazirani_estimate_test ===
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

## Benchmarks

`src/bench/bench_vazirani.cpp` times the real two-party circuit
(`pipeline/vazirani_pipeline.h`, the same one `main.cpp` runs) across a
**sweep** of four sizes, `VARS=CUBES` doubling from `4` to `32`, all at a
single **fixed, meaningful** epsilon/delta --
[ApproxMC](https://github.com/meelgroup/approxmc)'s own defaults,
`epsilon=0.8` (tolerance) and `delta=0.2` (failure probability, i.e.
success probability `>= 1-delta = 0.8`) -- rather than `main.cpp`'s small
fixed `K=3` smoke-test value.

It follows emp-toolkit's own benchmark conventions
(`emp-tool/docs/benchmark_conventions.md`) instead of a bespoke harness:
named `bench_<component>.cpp`; the timed region is wrapped in
`clock_start()`/`time_from()` (`emp-tool/runtime/core/utils.h`) around only
the protocol call itself, excluding unrelated setup (the local DIMACS
parse) so the reported time isn't billed for work the real protocol
wouldn't count either. Per-size network bytes/rounds are captured the same
way `emp-ot/bench/bench.h`'s `time_ot()` does -- an `io->send_counter` /
`io->recv_counter` / `io->rounds` snapshot before and after each size's
call, since one `NetIO` connection is reused across the whole sweep (its
own end-of-process printout would only give a session-wide total).

**Why this sweep now reaches `VARS=32` (it didn't used to)**:
`vazirani_trials` (`gadgets/vazirani/vazirani_estimate.h`) originally
computed `K` as quadratic in `(vars^2-1)` -- a bug (mixing up the
*variable* count with the conjoined DNF's *cube* count, and missing that
the real Vazirani bound is linear in the cube count, not quadratic). That
made `K` grow as `O(VARS^4)`, requiring billions of trials -- hours to
months -- for anything past `VARS=16` at a real epsilon/delta, which is
why an earlier version of this sweep either stopped at `VARS=16` or
quietly loosened epsilon past `1` (a vacuous, no-longer-real accuracy
bound) to keep going. The fix is linear in the cube count instead:
`K = ceil(min(1/delta, 3*ln(2/delta)) * (CUBES^2-1) / epsilon^2)` (see
"Vazirani estimate" above for the derivation) -- cheap enough to reach
`VARS=32` in under two minutes, measured live:

| VARS | M=CUBES² | K | K·M (work) | measured time |
|---|---|---|---|---|
| 4 | 16 | 118 | 1888 | 34.48 ms |
| 8 | 64 | 493 | 31552 | 480.12 ms |
| 16 | 256 | 1993 | 510208 | 5.88 s |
| 32 | 1024 | 7993 | 8184832 | 1.40 min |

(Full sweep, both parties, measured wall-clock: 90.5 s.)

Two implementation problems this sweep surfaced along the way (both
fixed, not workarounds specific to any one size -- they'd bite any
circuit at this scale):

1. **`emp::UInt_T`'s 64-bit reveal ceiling.** `UInt_T<Ctx,N>::clear_t` is a
   plain `uint64_t` -- `.reveal()`/`.input()`/`.constant()` can't
   round-trip more than 64 bits, a hard emp-toolkit limit, not something
   tunable. The final `VaziraniEstimate` width
   (`(VARS+bits_for(M)) + 17 + bits_for(K)`) stays under 64 bits through
   `VARS=16` (53 bits) but exceeds it at `VARS=32` (73 bits, at `K=7993`).
   `pipeline/vazirani_pipeline.h`'s `reveal_wide<W>()` handles the general
   case regardless of which size actually needs it: it slices any wide
   value into `<=64`-bit chunks (`UInt_T::slice<Lo,Hi>`, pure wiring, no
   extra gates), reveals each separately, and reassembles them into an
   `unsigned __int128` in plaintext -- which is why `run_vazirani_pipeline`
   returns `unsigned __int128`, not `uint64_t`.
2. **Stack overflow at large `K`/`M`.** Under the old, buggy quadratic
   formula, `pipeline/vazirani_pipeline.h`'s `reciprocals` local (an
   `array<DivideLookupResult<Ctx,PRODUCT>,K>`) reached well over a hundred
   megabytes at `K` in the hundreds of thousands -- several times over
   Linux's 8 MiB default stack. The corrected formula's `K` values are all
   small enough that this isn't actually triggered by the current sweep
   any more (`reciprocals` tops out at `2.20 MiB` at `VARS=32`), but
   `conjunction`/`weights`/`intervals` still scale with `M=CUBES^2`
   regardless of `K`, and would need this again well before a hypothetical
   `VARS=64` point. `bench_vazirani.cpp`'s `main()` still raises the soft
   `RLIMIT_STACK` to 256 MiB up front as cheap headroom (Linux grows the
   main thread's stack on demand up to whatever the limit is *at fault
   time*, so doing this after the process has already started still
   works) rather than requiring `ulimit -s` as an external prerequisite.

```sh
./build/bench_vazirani <party: 1=ALICE, 2=BOB> <path-to-dimacs-dnf-file>
```

Same two-party launch convention as `sh2pc_demo` -- two terminals, or
`./run.sh` adapted to point at `build/bench_vazirani` instead. The same
small bundled `alice.dnf`/`bob.dnf` (`4` vars, `2` cubes) is reused at
*every* size in the sweep: `dimacs_dnf::parse<VARS,CUBES>` only requires
the file's declared vars/cubes fit within the capacity, padding the rest
(see "DIMACS-DNF cube parser" above), and circuit cost is the same
regardless of how much of that capacity is "real" vs. padding, by design
-- so this is a valid way to benchmark performance at a given size without
a separately crafted DNF file per size. The whole sweep now finishes in
under two minutes (see the table above), so it's fine to just wait on it
interactively. Example output (`estimate` varies run to run, since it
depends on fresh joint-random samples each time):

```sh
# [alice] VARS=4 CUBES=4  epsilon=0.8 delta=0.2 K=118  estimate=9.27966  elapsed=34.48 ms  sent=4.58 MiB recv=399.00 B rounds=475
```

(Both parties print matching lines for every size -- confirmed by running
it live: Alice and Bob agree exactly on `estimate`. `elapsed` is formatted
by `format_duration()`, picking the largest readable unit -- us/ms/s/min/h
-- rather than always printing milliseconds: `VARS=32`'s `1.40 min` reads
fine either way at the current sweep sizes, but a raw `elapsed_us/1000.0`
in milliseconds prints in `std::ostream`'s default scientific notation
past `~1e6` (e.g. `elapsed=1.07004e+06 ms` for a multi-minute run, which
the old, buggy quadratic `vazirani_trials` routinely produced at
`VARS=16` -- see above), so `format_duration()` stays in place as
protection against that, not just for the sizes this sweep happens to hit
today.)

**Per-gadget breakdown.** Each size's summary line is followed by two
sorted (biggest-first) tables answering "which gadget dominates" --
network bytes and static memory, separately, since they're fundamentally
different kinds of measurement (see `pipeline/vazirani_pipeline.h`'s
`PipelineBreakdown`/`PipelineMemoryReport` for why). Bytes are formatted
with `format_bytes()` (binary units, KiB/MiB/GiB) matching
`emp::NetIO::get_statistics_string()`'s own convention -- the same one
already used by the "Network statistics:" printout that follows this
program's output, so bytes aren't reported two different ways in the same
console dump:

```sh
# [alice]     [net]  divide_lookup: sent=1.19 MiB recv=0.00 B total=1.19 MiB
# [alice]     [net]  select_cube_index: sent=1.10 MiB recv=0.00 B total=1.10 MiB
# [alice]     [net]  count_satisfied_cubes: sent=1003.00 KiB recv=0.00 B total=1003.00 KiB
# [alice]     [net]  cube_at_index: sent=719.06 KiB recv=0.00 B total=719.06 KiB
# [alice]     [net]  sample_in_range: sent=424.06 KiB recv=0.00 B total=424.06 KiB
# [alice]     [net]  vazirani_estimate: sent=118.84 KiB recv=0.00 B total=118.84 KiB
# [alice]     [net]  random_assignment: sent=29.50 KiB recv=0.00 B total=29.50 KiB
# [alice]     [net]  cube_weights: sent=23.50 KiB recv=0.00 B total=23.50 KiB
# [alice]     [net]  conjoin_dnf: sent=19.00 KiB recv=0.00 B total=19.00 KiB
# [alice]     [net]  cube_intervals: sent=4.00 KiB recv=0.00 B total=4.00 KiB
# [alice]     [net]  dnf_weight: sent=3.75 KiB recv=0.00 B total=3.75 KiB
# [alice]     [net]  trial_random_input_feeding: sent=0.00 B recv=354.00 B total=354.00 B
# [alice]     [net]  reveal: sent=33.00 B recv=33.00 B total=66.00 B
# [alice]     [net]  cube_input_feeding: sent=0.00 B recv=12.00 B total=12.00 B
# [alice]     [mem]  reciprocals (K divide_lookup outputs): 33.19 KiB
# [alice]     [mem]  conjunction (conjoin_dnf's output): 3.00 KiB
# [alice]     [mem]  intervals (cube_intervals' output): 2.66 KiB
# [alice]     [mem]  alice_cubes + bob_cubes: 1.50 KiB
# [alice]     [mem]  weights (cube_weights' output): 1.50 KiB
# [alice]     [mem]  one trial's live locals (selected/assignment/satisfied_count): 416.00 B
```

(`VARS=4` above, `K=118` -- bandwidth is dominated by the per-trial
gadgets, each running `K` times: `select_cube`'s three pieces
(`sample_in_range`/`select_cube_index`/`cube_at_index`, called directly
in `pipeline/vazirani_pipeline.h` rather than through the composed
`select_cube()`, specifically so each gets its own breakdown entry
instead of one combined bucket) together roughly match
`divide_lookup`/`count_satisfied_cubes`, while memory is dominated by
`reciprocals`, the one structure that's actually `K`-sized. `reciprocals`
stays on top at every size in the current sweep (`33.19 KiB` vs
`conjunction`'s `3.00 KiB` at `VARS=4`; still ahead but much closer,
`2.20 MiB` vs `1.06 MiB`, by `VARS=32`), since `K` still outpaces `M` at
these sizes even under the corrected, much-gentler formula -- though the
narrowing gap suggests an `M`-sized structure could plausibly take over
at a size beyond what's currently swept. Which gadget "wins" depends on
which of `K` or `M` a given point favors, which is why both get reported
per size instead of just once.)

**Network breakdown** (`PipelineBreakdown`, `pipeline/vazirani_pipeline.h`)
wraps each gadget call in a `measure(io, breakdown, name, fn)` helper that
snapshots `io->send_counter`/`recv_counter` before and after, the same
before/after pattern the top-level per-size numbers already use (see
above) -- just at finer granularity, one snapshot per gadget instead of
one for the whole pipeline. Per-trial gadgets (`sample_in_range`,
`select_cube_index`, `cube_at_index`, `random_assignment`,
`count_satisfied_cubes`, `divide_lookup`) accumulate into the *same*
named entry across all `K` trials, so e.g. `sample_in_range`'s number is
that gadget's total over the whole run, not one trial's share.
`run_vazirani_pipeline`'s `io`/`breakdown` parameters both default to
`nullptr`; `src/main.cpp` never passes either, so `measure()` degrades to
a plain direct call there -- the demo pays nothing for this.

**Memory breakdown** (`PipelineMemoryReport`, same file) is a *computed*
report, not a runtime measurement: each structure's exact byte size
(`sizeof(type) * count`) is known from its type alone. Runtime process
memory (`getrusage().ru_maxrss`) was considered and rejected for this --
it's a cumulative high-water mark that doesn't cleanly reset between
gadgets, and most of what a gadget allocates is stack space freed the
moment its function returns, so a live snapshot would mostly measure
noise rather than each gadget's actual footprint.

To change the sweep's sizes or the fixed epsilon/delta, edit the
`EPSILON`/`DELTA` constants and the `run_one<VARS>(...)` calls in
`main()` and rebuild -- `VARS`/`CUBES`/`K` are compile-time, like every
other circuit-shape constant in this codebase (see "Putting it
together"), so there's no runtime flag for them.

## Layout

- `CMakeLists.txt` -- finds/links `emp-tool`, `emp-ot`, `emp-sh2pc`, OpenSSL, GMP.
- `src/main.cpp` -- interactive two-party demo (small fixed `K`) of the pipeline below (see "Putting it together").
- `src/pipeline/` -- the two-party circuit itself, shared by `main.cpp` and `src/bench/`:
  - `vazirani_pipeline.h` -- `run_vazirani_pipeline<VARS,CUBES,K>` (returns `unsigned __int128`, via the file's own `reveal_wide<W>()`), `SH2PCSession`-only (not `Ctx`-generic like `gadgets/`); also `PipelineBreakdown`/`measure()` (optional per-gadget network accounting) and `PipelineMemoryReport`/`pipeline_memory_report<VARS,CUBES,K>()` (computed per-structure byte sizes) -- see "Benchmarks".
- `src/bench/` -- benchmarks, following emp-toolkit's own `bench_<component>.cpp` convention (see "Benchmarks"):
  - `bench_vazirani.cpp` -- times the real circuit across a `VARS=CUBES` sweep from `4` to `32`, all at ApproxMC's default `epsilon=0.8, delta=0.2`, with a per-gadget network/memory breakdown at every size.
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
  - `vazirani/` -- the Vazirani estimation stage:
    - `select_cube.h` -- samples a joint random value, then selects and looks up the cube it lands in.
    - `random_assignment.h` -- extends a selected cube into a full random satisfying assignment.
    - `count_satisfied_cubes.h` -- counts how many cubes in an array an assignment satisfies.
    - `divide_lookup.h` -- oblivious fixed-point `1/count` lookup table.
    - `vazirani_estimate.h` -- combines `K` trials' reciprocals and the total weight into the raw Vazirani estimate.
- `src/tests/` -- unit tests, all built into the single `run_tests` binary:
  - `run_tests.cpp` -- the shared `main()`; calls each suite below.
  - `bits_of.h` -- pure-stdlib `bits_of<N>(s)` helper (a `'0'`/`'1'` string -> `std::array<bool,N>`), shared by every test file including `dimacs_dnf_test.cpp`.
  - `test_helpers.h` -- emp-tool dependent helpers built on `bits_of.h`: `bits_of_uint<WIDTH>` and `make_cube<Session,Ctx,N>`, shared by every gadget test file.
  - `dimacs_dnf_test.cpp` -- tests for `utils/dimacs_dnf.h`.
  - `dnf/` -- tests for `gadgets/dnf/`:
    - `dnf_distribute_test.cpp`, `cube_weight_test.cpp`, `dnf_weight_test.cpp`, `cube_intervals_test.cpp` (all `emp::ClearSession`-based, no network).
  - `vazirani/` -- tests for `gadgets/vazirani/`:
    - `select_cube_test.cpp`, `random_assignment_test.cpp`, `count_satisfied_cubes_test.cpp`, `divide_lookup_test.cpp`, `vazirani_estimate_test.cpp` (all `emp::ClearSession`-based, no network).
- `sample.dnf` -- a standalone example DIMACS-DNF file (used by `dimacs_dnf_test.cpp`'s docs, not read by any binary).
- `alice.dnf` / `bob.dnf` -- the two parties' example private inputs to `main.cpp` and `bench_vazirani`.
- `run.sh` -- launches both parties of `sh2pc_demo` locally for a quick check.
