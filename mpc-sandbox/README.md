# mpc-sandbox

Minimal two-party garbled-circuit sanity check built on
[emp-toolkit](https://github.com/emp-toolkit) (`emp-tool` + `emp-ot` +
`emp-sh2pc`, semi-honest 2PC). Not related to any real MPC protocol logic --
it just proves the whole pipeline (garbling, OT, network setup, evaluation,
output reveal) works end to end before building anything real on top of it.

Alice and Bob each hold a private 32-bit integer; the circuit reveals their
sum and their max to both parties.

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
./build/sh2pc_demo <party: 1=ALICE, 2=BOB> <secret-uint32>
```

Two terminals:

```sh
# terminal 1
./build/sh2pc_demo 1 7

# terminal 2
./build/sh2pc_demo 2 5
```

Or both at once via the helper script:

```sh
./run.sh 7 5
# [alice] my input=7  sum=12  max=7
# [bob]   my input=5  sum=12  max=7
```

Alice listens on a fixed localhost port (default `12345`); Bob connects to
it. Override with the `EMP_PORT` (port) and `EMP_PEER_IP` (Bob's target
address) environment variables if needed.

## Layout

- `CMakeLists.txt` -- finds/links `emp-tool`, `emp-ot`, `emp-sh2pc`, OpenSSL, GMP.
- `src/main.cpp` -- the two-party sum/max demo.
- `run.sh` -- launches both parties locally for a quick check.
