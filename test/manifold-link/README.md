# test/manifold-link — link upstream manifold against the shim

Phase 7-precursor (B1 in the planning shorthand): prove that the
elalish/manifold C++ library builds + links against the shim with
zero unexpected wasm imports. **Not** a "manifold tests run on wasm"
test — that's B2, which would also need GoogleTest on wasm and is
out of v0.1.x scope.

## What this exercises

- manifold's full C-API library (`manifoldc`) compiled for
  `wasm32-unknown-unknown`.
- On manifold 3.x pins only, Clipper2 (its 2D Boolean dependency then)
  ditto. The default pin, manifold master, replaced Clipper2 with its
  own 2D boolean engine.
- A tiny `probe.c` calling manifold's C API:
  `manifold_cube` → `manifold_translate` → `manifold_boolean(ADD)`
  → `manifold_num_tri`.
- The link succeeds with **zero unexpected imports** in the resulting
  `.wasm` (asserted by `manifold_link_imports_check` ctest entry,
  reusing `test/smoke/check-imports.sh`).

If a new symbol surfaces during linking, that's the exact signal this
test exists to produce: add the symbol to `libcxx/src/stubs.cpp`,
re-run, repeat. Each addition is its own follow-up commit/PR per the
"grow by demand" rule in `docs/context.md`.

## Why FetchContent, not submodule

See the `docs/context.md` section on this. Short version: submodules
are operationally fussy without buying us anything extra over
`GIT_TAG`-pinned FetchContent. Vendoring manifold's source into this
repo would defeat the "minimal shim" framing.

## Pinned upstream versions

The shim's `wasm_cxx_shim_add_manifold()` helper bundles a known-good
manifold pin (see `cmake/WasmCxxShimManifold.cmake`'s default
`MANIFOLD_GIT_TAG`). On 3.x pins, Clipper2's pin is owned by
manifold's own `cmake/manifoldDeps.cmake`; the shim does not
pre-declare it.

Each shim release verifies a specific manifold pin in CI; see
`CHANGELOG.md`'s "Tested upstream combinations" table for the
mapping.

## Carry-patches

None at v0.4.0+. Manifold's `MANIFOLD_NO_IOSTREAM` build option is
now upstream
([elalish/manifold#1690](https://github.com/elalish/manifold/pull/1690),
merged 2026-05-04). The helper sets the option ON; manifold owns the
rest. (On 3.x pins that includes the bundled Clipper2 carry-patch that
mirrors [AngusJohnson/Clipper2#1094](https://github.com/AngusJohnson/Clipper2/pull/1094).)

Callers can still pass `EXTRA_MANIFOLD_PATCHES` to apply their own
patches against the pinned manifold checkout if needed.

## Stub headers

Both live in `include/` and shadow libc++ via the -isystem chain.
Each addresses a header path that libc++ gates behind
`#if _LIBCPP_HAS_THREADS` (which the shim's `__config_site` sets to
0), but which manifold's source still references unconditionally.
With threads off, the stubbed operations are trivially correct.

- `include/mutex` — no-op `std::mutex` / `std::recursive_mutex` /
  `std::lock_guard` / `std::scoped_lock` / `std::unique_lock`.
  Referenced by manifold's `utils.h` and `impl.h`. Stub locks are
  no-ops (single-threaded code can't race).
- `include/memory` — `#include_next <memory>` plus stubs for the
  `std::atomic_load/store/exchange/is_lock_free(shared_ptr*)` free
  function family. Used by manifold v3.5.0+ for the
  `ExecutionContext` attached to `Manifold` via
  `std::shared_ptr<ExecutionContext> ctx_`. Stub versions are
  no-op-with-direct-copy.

Upstream-friendly fix for both: a `MANIFOLD_NO_THREADS` build flag
that gates the mutex/atomic-shared_ptr usage out. Until that lands,
stubs.

## Status — green

manifold's library + C API bindings compile against the shim, link
with **zero unexpected wasm imports**, and the probe runs under Node
returning a sane triangle count for a boolean-union operation. Two
ctest entries cover this:

- `manifold_link_imports_check` — wasm-objdump assertion that the
  Import section is absent.
- `manifold_link_run` — Node ESM runner, asserts `probe_run()` returns
  a positive integer (the triangle count from
  `union(cube, translate(cube, 0.5, 0.5, 0.5))`; today it's 36).

What it took to get green:

1. **`MANIFOLD_PAR=OFF`** — manifold's parallel backend is a hard
   dependency on TBB/OpenMP we don't ship.
2. **CMake auto-injection fix** in top-level CMakeLists
   (`CMAKE_<LANG>_IMPLICIT_INCLUDE_DIRECTORIES ""` post-`project()`).
   Stops CMake from prepending clang's resource dir as `-isystem`
   before user `-isystem` paths, which was breaking libc++'s
   `<cstddef>` → `<stddef.h>` resolution chain. Broadly useful — not
   manifold-specific.
3. **`MANIFOLD_NO_IOSTREAM=ON`** — manifold's native build option
   (added in elalish/manifold#1690, merged 2026-05-04) propagates
   through to `MANIFOLD_NO_FILESYSTEM` and `CLIPPER2_NO_IOSTREAM`,
   stripping stream- and filesystem-using bits from the public API
   + tests.
4. **Stub `include/mutex`** — no-op `std::mutex`/`recursive_mutex`/
   `scoped_lock`, because libc++ gates these behind
   `_LIBCPP_HAS_THREADS`. `lock_guard`, `unique_lock` and the lock tags
   come from libc++'s own `<__mutex/*.h>` headers, which are not gated
   and which other headers (`<memory_resource>`) include too, so
   defining them here would collide.
5. **`libcxx-extras.cpp`** — provides the libc++ source-file symbols
   that the main libcxx component intentionally doesn't ship:
   `std::nothrow`, `std::__1::__shared_count::~__shared_count`,
   `std::__1::__shared_weak_count::~__shared_weak_count` /
   `__release_weak` / `lock` / `__get_deleter`, `bad_weak_ptr` key
   functions, `std::align`, a fake `std::chrono::steady_clock::now`
   that advances one nanosecond per call (manifold's Boolean2 timing
   diagnostics name the clock; they are off by default),
   `std::__throw_system_error` as a trap (reachable from `unique_lock`
   misuse), and an adapted port of `std::pmr` (`get_default_resource`
   and friends, plus an `unsynchronized_pool_resource` that serves every
   request from its adhoc pool; `synchronized_pool_resource` and
   `monotonic_buffer_resource` are not provided and fail to link).
   Scoped to this test, NOT part of the
   main libcxx component (which stays insulated from `<memory>` /
   `<new>` includes for libc++ version-drift safety).
6. **Clipper2 utilities/tests/examples disabled** (3.x pins) via
   `CLIPPER2_TESTS=OFF`, `CLIPPER2_UTILS=OFF`, `CLIPPER2_EXAMPLES=OFF`
   — defaults are ON and pull in `<sys/types.h>`, googletest, etc.
   we don't ship.

## Upstream PR status

[elalish/manifold#1690](https://github.com/elalish/manifold/pull/1690)
landed 2026-05-04 — the carry-patch dropped at v0.4.0. A bundled
Clipper2 tracking patch for
[AngusJohnson/Clipper2#1094](https://github.com/AngusJohnson/Clipper2/pull/1094)
travels with the manifold pin (manifold owns it via its
`manifoldDeps.cmake` carry-patch). Drops once Clipper2 takes #1094
and manifold's pin moves past it.

## Opt-in build

```sh
cmake --preset wasm32 -DWASM_CXX_SHIM_BUILD_MANIFOLD_LINK=ON
cmake --build --preset wasm32 --target manifold-link-probe
ctest --preset wasm32 -R manifold_link
```

Default is `OFF` — the FetchContent clone of manifold +
manifold's transitive deps takes minutes and isn't something every
developer needs on every edit-build loop. CI runs it in its own job.

## Expected outcomes (when working)

- `manifold-link-probe.wasm` builds without errors.
- Imports section of the produced `.wasm` is empty (only `__heap_base`
  expected, and that's resolved at link time).
- `probe_run` is exported.
- Eventually: a Node runner that calls `probe_run` and asserts a
  reasonable triangle count (post-stub-completion, when the runtime
  symbols are all there).

## Pre-completion state (during iteration)

If you see `wasm-ld --warn-unresolved-symbols` reporting a list of
missing symbols, that's the test working as designed: each entry is
a candidate addition to `libcxx/src/stubs.cpp` (or to the
appropriate component). Triage by name pattern:

- `_Z*` C++ mangled → `libcxx/src/stubs.cpp`
- `__cxa_*`         → `libcxx/src/cxa.cpp`
- A POSIX/C name    → `libc/`, but pause first: most POSIX APIs are
                      out of scope for the shim, and the right answer
                      may be a manifold-side patch rather than a libc
                      addition. Check `docs/context.md`'s scope rules.
