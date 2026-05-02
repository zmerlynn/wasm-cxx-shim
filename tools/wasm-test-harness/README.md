# wasm-test-harness

Minimal test harness for running consumer test suites on
`wasm32-unknown-unknown` via the shim. Generic mechanism — each
consumer's test framework gets an adapter under `adapters/`.

## Why this exists

Consumer C++ libraries ship with their own test suites (typically
GoogleTest-based). Running those tests on `wasm32-unknown-unknown`
validates the shim against real-world code, but the test frameworks
themselves use iostream / locale / threads / filesystem that the shim
deliberately doesn't ship. Patching the test framework is a
multi-day-and-ongoing effort; rewriting consumer tests against a
different framework is a non-starter.

This harness is the third option: a tiny in-house registry + runner
(<400 LOC) plus a translation header per consumer's test framework
that maps their macros to ours. Consumer test sources compile
unmodified.

## Layout

```
tools/wasm-test-harness/
├── include/wcs-test.h    Macros + extern "C" registry/runner API
├── src/harness.c         Registry, runner, log buffer (C, not C++)
├── adapters/
│   └── gtest/
│       └── gtest/gtest.h Translation: GoogleTest macros → WCS_*
├── test/                 Self-tests: registry counts, REQUIRE-abandons-test
│                         semantics, log format. Validates the harness
│                         independent of any consumer.
├── run.mjs               Node runner: load wasm, invoke, read log
└── CMakeLists.txt        Builds the harness as a static archive
```

## Macro coverage (current)

For consumer tests using GoogleTest, the adapter at
`adapters/gtest/gtest/gtest.h` covers:

- `TEST(suite, name)`
- `EXPECT_TRUE`/`FALSE`, `EXPECT_EQ`/`NE`/`LT`/`LE`/`GT`/`GE`
- `ASSERT_TRUE`/`FALSE`, `ASSERT_EQ`/`NE`/`LT`/`LE`/`GT`/`GE`
- `EXPECT_FLOAT_EQ`/`DOUBLE_EQ`/`NEAR` and `ASSERT_*` variants
- `::testing::InitGoogleTest` (no-op)
- `RUN_ALL_TESTS()` (calls `wcs_run_tests`)

Not supported (yet):

- Test fixtures (`TEST_F`, `::testing::Test` base class)
- Parameterized tests (`TEST_P`)
- Custom matchers (`EXPECT_THAT`)
- Death tests
- GMock

If a consumer's tests need these, extend the adapter or skip those
tests.

## ASSERT vs EXPECT semantics

Real GoogleTest's `ASSERT_*` macros use a non-local mechanism (early
return / fatal failure) to abandon the current test on the first
failure. This adapter implements that with `return` from the test
function — which works for the common case (`ASSERT` directly inside
`TEST(...)`) but does NOT propagate across helper-function boundaries.

If a consumer test's helper function calls `ASSERT_*`, the helper
returns but the test body continues. Most tests don't care. For tests
that genuinely depend on early-exit-from-helper semantics, rewrite to
check + return-bool, or hoist the assertion to the test body.

## How a consumer wires this up

In the consumer's wasm-test-build CMakeLists:

1. `target_include_directories(... BEFORE -isystem
    ${shim}/tools/wasm-test-harness/adapters/gtest)` — puts our
    `gtest/gtest.h` ahead of the real GoogleTest's.
2. `target_include_directories(... -isystem
    ${shim}/tools/wasm-test-harness/include)` — for `<wcs-test.h>`.
3. Compile the consumer's `*_test.cpp` files. Their
    `#include "gtest/gtest.h"` resolves to ours; macros translate.
4. Link against `wasm-cxx-shim-test-harness.a` plus the consumer
    library, the shim's libc/libm/libcxx, plus any consumer-specific
    extras (`libcxx-extras.cpp` for shared_ptr machinery, etc.).
5. Export `wcs_run_tests`, `wcs_log_buffer`, `wcs_log_size`, `memory`.
6. Run via `node tools/wasm-test-harness/run.mjs <wasm>`.

For an end-to-end example, see `test/manifold-tests/`.

## Output

The runner produces a stream of log lines:

```
wasm-test-harness: starting...
[   1/  47] Boolean.Tetra ... ok
[   2/  47] Boolean.MeshGLRoundTrip ... ok
[   3/  47] Boolean.HypotheticalRegression ... FAILED (3 assertion failures)
    FAIL boolean_test.cpp:123: m.NumTri() == 12
    FAIL boolean_test.cpp:124: m.IsEmpty() == false
    FAIL boolean_test.cpp:131: la::length(...) < 0.001
[   4/  47] ...
wasm-test-harness: 44 passed, 3 failed, 47 total
```

(Real example: this PR's `manifold_tests_run` reports
`47 passed, 0 failed, 47 total` — the failure example above is
illustrative.)

`run.mjs` exits with status 0 if the failure count is zero, 1 otherwise.

## Why the harness is C, not C++

The harness implementation deliberately uses C, not C++:

- Avoids dragging libcxx into the harness archive itself.
- The registry uses static arrays + plain function pointers; no
  `std::vector`, no allocator dependency.
- Linkage to consumer test code is via `extern "C"` so the registry
  works regardless of the consumer's C++ ABI flags.

The macros in the public header are usable from both C and C++; the
runtime is C all the way down.
