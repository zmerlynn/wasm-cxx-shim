# test/manifold-tests — manifold's own test suite, on the shim

Phase 7-B2: take a portion of upstream manifold's CI and run it under
the shim. Validates end-to-end correctness of the full stack
(libc + libm + libcxx + libcxx-extras + manifold) via a
real consumer's tests, not just a probe.

Currently runs six test files from upstream manifold at the default pin
(manifold master `74493803`) — **225/225 tests pass**. One of them,
`CrossSection.ConcurrentConstAccessorsAreRaceFree`, cannot do what its
name says here: the `<thread>` stand-in (see below) runs each "thread"
to completion in turn, so it exercises the code serially and cannot
catch a race.

| File | Surface |
|---|---|
| `test/boolean_test.cpp`         | 3D Boolean ops (union, difference, intersect, Minkowski, ...) |
| `test/boolean_complex_test.cpp` | Complex Boolean fixtures (gear permutations, sphere/cube unions, ...) |
| `test/cross_section_test.cpp`   | 2D ops via manifold's boolean2 engine (offset, hull, fill rules, decompose) |
| `test/manifoldc_test.cpp`       | C-ABI bindings (CSG ops, transforms, level set) |
| `test/sdf_test.cpp`             | Signed distance fields → marching cubes; libm-heavy |
| `test/smooth_test.cpp`          | Surface smoothing (Csaszar, mirrored, gyroid, ...) |

## What this exercises

- manifold's `test/test_main.cpp` compiled directly (not vendored),
  with `MANIFOLD_NO_FILESYSTEM=1` ifdef'ing out the `main()` and
  filesystem-based fixture I/O. Provides `Options options;` and the
  helpers (`ExpectMeshes`, `RelatedGL`, `CheckGL`, `CheckStrictly`,
  `CubeSTL`, `CubeUV`, ...) that consumer test files call.
- The test files (`boolean_test.cpp`, `sdf_test.cpp`,
  `cross_section_test.cpp`) compiled against the gtest-shim adapter
  at `tools/wasm-test-harness/adapters/gtest/` — `TEST(...)`,
  `EXPECT_*`, and `ASSERT_*` macros expand to `WCS_*` calls in the
  harness.
- `wasm-test-harness` library (C only): registry of registered tests,
  log buffer, runner that walks the registry and reports counts via
  the `wcs_run_tests` export.
- Node ESM runner at `tools/wasm-test-harness/run.mjs` that loads the
  wasm, calls `wcs_run_tests()`, prints the captured log, exits with
  the failure count.

## How it's wired

```
                              (consumer test)
                       boolean_test.cpp
                              │
              #include "gtest/gtest.h"
                              │
                              ▼
        adapters/gtest/gtest/gtest.h   (adapter, on -isystem first)
                              │
                       maps to WCS_* macros
                              │
                              ▼
                   include/wcs-test.h    (harness primitives)
                              │
                              ▼
           src/harness.c     (registry + runner + log buffer)
                              │
                              ▼
           wcs_run_tests()   (wasm export, called from Node)
```

The adapter is on the include path BEFORE manifold's vendored
GoogleTest, so `#include "gtest/gtest.h"` resolves to it. Test sources
compile unmodified.

## Adding more test files

Append the manifold test source path to `_manifold_test_files` in
`CMakeLists.txt`:

```cmake
set(_manifold_test_files
    ${_manifold_src}/test/test_main.cpp
    ${_manifold_src}/test/boolean_test.cpp
    ${_manifold_src}/test/boolean_complex_test.cpp
    ${_manifold_src}/test/cross_section_test.cpp
    # ... existing entries ...
    ${_manifold_src}/test/your_new_test.cpp   # <- add new ones here
)
```

Things that may need extending when you do:

- **The gtest-shim** at `tools/wasm-test-harness/adapters/gtest/` — if
  the new test file uses macros the adapter doesn't translate yet
  (test fixtures, parameterized tests, custom matchers), extend the
  adapter or rewrite the test. Things explicitly NOT supported today:
  `TEST_F`, `TEST_P`, `EXPECT_THAT`, `EXPECT_DEATH`, GMock.
- **The libcxx-extras object** at `test/manifold-link/libcxx-extras.cpp`
  — new tests may exercise `<memory>` / `<new>` paths that need
  additional libc++ source-file symbols.
- **`samples` library**: tests that include `samples.h` (e.g.,
  `hull_test.cpp`, `properties_test.cpp`, `samples_test.cpp`) need the
  manifold/samples helper library + sample-geometry sources wired in.
  Manifold gates `samples` on `MANIFOLD_TEST=ON` which we have OFF, so
  it's not currently built.
- **Direct `<set>` use**: tests like `manifold_test.cpp` reference
  `std::set` without a direct `#include <set>`, relying on transitive
  pulls from headers our libcxx subset doesn't ship. Adding such a test
  means either patching the test source or extending the libcxx subset.

## Test-only stand-in headers

`include/` here is on the include path for these tests only, ahead of
libc++:

- `thread` — a `std::thread` that runs its function in the constructor.
  `cross_section_test.cpp` starts threads in a test gated only on
  `#ifndef __EMSCRIPTEN__`, and with `_LIBCPP_HAS_THREADS=0` libc++
  declares no `std::thread`.
- `iostream` — real gtest.h pulls in `<iostream>`, and manifold's tests
  use `std::cerr` from it. With libc++'s localization off there is no
  usable stream, so this declares discarding `cerr` / `cout` / `clog` /
  `endl` / `flush` in libc++'s namespace. With localization on it
  forwards to the real header.

## Iostream/filesystem stripping

This directory's tests build with `MANIFOLD_NO_IOSTREAM=ON` (set by
the helper, native upstream option as of manifold#1690). That
transitively strips iostream/filesystem-using bits from manifold's
library code and its `test/test_main.cpp` fixture helpers (and, on
3.x pins, the bundled Clipper2 headers). No carry-patches needed at v0.4.0+;
documented further in `test/manifold-link/README.md`.

## Build & run

```sh
# Configure with manifold ON (heavyweight)
cmake --preset wasm32 -DWASM_CXX_SHIM_BUILD_MANIFOLD_LINK=ON

# Build
cmake --build --preset wasm32 --target manifold-tests

# Run via ctest
ctest --preset wasm32 -R manifold_tests --output-on-failure

# Or directly via the harness runner (more verbose output)
node tools/wasm-test-harness/run.mjs \
    build/wasm32/test/manifold-tests/manifold-tests.wasm
```

Expected output ends with: `wasm-test-harness: 225 passed, 0 failed, 225 total`.

## Why this exists

The "shim works for real C++ kernels" argument is more convincing
when you can point at a consumer's own test suite running green on
top of it, vs. a probe that exercises one code path. As we add more
upstream consumers (manifold-csg, future others), the same harness
mechanism applies: drop a small adapter for whichever test framework
the consumer uses, point `_manifold_test_files`-equivalent at their
sources, get the same kind of confidence cheaply.
