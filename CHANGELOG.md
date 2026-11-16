# Changelog

All notable changes to wasm-cxx-shim are documented here.

The shim follows [semantic versioning](https://semver.org/), with the
caveat that for the v0.x series symbol additions are minor bumps and
breaking changes are also minor (we're explicitly volatile until v1.0).

## Tested upstream combinations

The shim's `wasm_cxx_shim_add_manifold()` helper bundles a known-good
pin of manifold + Clipper2 with the carry-patches that make them link
on `wasm32-unknown-unknown`. Each tagged shim release verifies the
combination in CI:

| Shim version          | manifold                                      | Clipper2                                     | Patches shipped       |
|-----------------------|-----------------------------------------------|----------------------------------------------|-----------------------|
| unreleased (`main`)   | `46e5d2c9` (master, pre-4.0; no Clipper2)     | none (manifold master dropped it)            | 0                     |
| v0.5.0                | `v3.5.0` (also verified against `v3.5.2`)     | inherits manifold's pin                      | 0                     |
| v0.4.0                | `3ce9622b` (master, includes elalish/manifold#1690) | inherits manifold's pin (manifold owns the FetchContent_Declare) | 0                     |
| v0.4.0-alpha.1        | `5f95a3ac` (master + vendored elalish/manifold#1690) | inherits manifold's pin (manifold owns the FetchContent_Declare) | 1 (verbatim diff of #1690) |
| v0.3.0                | `v3.4.1`                                      | `46f639177fe418f9689e8ddb74f08a870c71f5b4`   | 0001 + 0002 + 0003    |
| v0.2.0                | `v3.4.1`                                      | `46f639177fe418f9689e8ddb74f08a870c71f5b4`   | 0001 + 0002 + 0003 (carried in `test/manifold-link/patches/`; helper not yet present) |

Consumers calling `wasm_cxx_shim_add_manifold()` (introduced in v0.3.0)
with no arguments get the row matching their installed shim version.
Pinning to other refs is supported via `MANIFOLD_GIT_TAG` /
`EXTRA_MANIFOLD_PATCHES` parameters; see
`cmake/WasmCxxShimManifold.cmake`.

When upstream PRs land for the carry-patches, the corresponding patch
drops from a future shim release and the table notes the upstream
version where the macro guard appears natively.

---

## Unreleased

Catches up to manifold master at `46e5d2c9` (pre-4.0). Upstream replaced
Clipper2 with its own 2D boolean engine ("boolean2") and changed its C
API to return handles instead of taking caller-allocated memory. Two
libm functions and two libc++ source-file pieces were missing.

### Added

- `libm`: `frexp` (boolean2) and `fmod` (manifold's own tests), vendored
  from wasi-libc's musl tree like the rest. 44 `.c` files now, each
  with a `libm-check` test.
- `test/manifold-link/libcxx-extras.cpp`: a `steady_clock::now()` stub
  that returns the epoch, and a reduced port of libc++'s
  `src/memory_resource.cpp` (`get_default_resource` and friends, plus
  an `unsynchronized_pool_resource` that serves every request from its
  adhoc pool). boolean2 names the clock in its timing diagnostics, off
  by default, and allocates through the pool resource.
- `test/manifold-tests/include/thread`: a test-only `std::thread` that
  runs its function synchronously, for an upstream test gated only on
  `#ifndef __EMSCRIPTEN__`.
- gtest adapter: assertions accept a streamed message
  (`EXPECT_NEAR(a, b, eps) << "context"`), as real GoogleTest's do.
  `SCOPED_TRACE` is a no-op, and when libc++ has no iostreams the
  adapter provides a discarding `std::cerr` / `std::cout` /
  `std::endl` (real gtest.h includes `<ostream>`, and manifold's tests
  rely on that).

### Changed

- Default `MANIFOLD_GIT_TAG` moved to manifold master `46e5d2c9`.
  A 3.x tag still works as an override: the helper keeps its harmless
  `CLIPPER2_*` options and the test links pick up `Clipper2` only when
  that target exists. `probe.c` uses the new handle-returning C API,
  so it would need the 3.x form with a 3.x pin.
- `test/smoke/include/__config_site`: `_LIBCPP_HAS_MONOTONIC_CLOCK` is
  now 1, which declares `steady_clock`. Nothing calls a real clock: the
  extras stub above defines `now()`, and `clock_gettime` stays
  unimplemented (the negative-link test still checks that).
- `test/manifold-link/include/mutex`: takes `lock_guard`,
  `unique_lock` and the lock tags from libc++'s `<__mutex/*.h>`
  instead of redefining them. Those headers are not gated on threads,
  and boolean2 pulls in `<memory_resource>`, which includes them, so
  the old copies collided.
- Inner manifold-tests count: 121 to 225, from upstream's new tests in
  the same six files. Sizes: `manifold-link-probe.wasm` 392 KB to
  405 KB, `manifold-tests.wasm` 1.16 MB to 1.37 MB, both inside their
  budgets.

### Earlier on main (the v3.5.2 pin, never released on its own)

- Default `MANIFOLD_GIT_TAG` bumped from `v3.5.0` to `v3.5.2`. No
  version bump: `libc`, `libm`, and `libcxx` are untouched, so v0.5.0
  as tagged also builds against `v3.5.2` — point `MANIFOLD_GIT_TAG` at
  it and you get the same result. v3.5.0's `ExecutionContext`
  groundwork (the `assert.h` and atomic-`shared_ptr` gaps closed in
  v0.5.0) already covered the symbol surface the two upstream patch
  releases build on — nothing new was missing.
- Inner manifold-tests count: 120 → 121. The addition is
  `CBIND.execution_context_factories`, covering the new
  `manifold_execution_context_{of_meshgl,level_set,smooth}` C entry
  points. Upstream also added
  `Boolean.ConcurrentSharedNodeOriginalIDStable` and
  `Boolean.ConcurrentSharedOpNodeOriginalIDStable`, both inside
  `#if MANIFOLD_PAR == 1` and so absent from the serial build.
- `manifold-tests.wasm` and `manifold-link-probe.wasm` sizes are flat
  (1.16 MB / 392 KB); both budgets unchanged.
- Doc-staleness sweep: README's helper description still described the
  three carry-patches dropped back in v0.4.0, and `docs/plan.md` still
  had the v0.5.0 phase marked in-flight.

### Notes for downstream consumers

- Upstream's new `test/context_test.cpp` (56 tests, most of them
  `MANIFOLD_PAR == 1`-gated) is not in the inner test list: it
  includes `<thread>` and `<chrono>` at file scope, neither of which
  the libcxx subset ships. Adding it means two more stub headers for
  a handful of serial-build tests.

## v0.5.0 — manifold v3.5.0 catch-up (2026-05-23)

Bumps the manifold pin to `v3.5.0` (the upstream release, 20 commits
past v0.4.0's pin). Two symbol gaps surfaced and were closed; no
carry-patches were reintroduced.

### Added

- `libc/include/assert.h` — minimal freestanding implementation
  (`__builtin_trap()` on failure, `static_assert` → `_Static_assert`
  for C). Surfaced by manifold v3.5.0's new `disjoint_sets.h` which
  includes `<cassert>`; libc++'s `<cassert>` falls through to
  `<assert.h>` via `__has_include`, and the shim previously didn't
  ship one. Generally useful — not manifold-specific.
- `test/manifold-link/include/memory` — `#include_next <memory>` +
  stubs for the `std::atomic_load/store/exchange/is_lock_free(shared_ptr*)`
  free function family. libc++ gates these behind
  `#if _LIBCPP_HAS_THREADS` (which `__config_site` sets to 0).
  Manifold v3.5.0 attaches `ExecutionContext` to `Manifold` via
  `std::shared_ptr<ExecutionContext> ctx_` and accesses it with
  these atomic-for-shared_ptr free functions; without the stubs,
  the compile fails with "no matching function for call to
  atomic_load". Same pattern as the existing `<mutex>` stub:
  single-threaded freestanding code is correct without atomicity.

### Changed

- Default `MANIFOLD_GIT_TAG` bumped from `3ce9622b` to `v3.5.0`.
- Inner manifold-tests count: 121 → 120. The dropped test is
  `Smooth.SineSurface`, removed upstream in v3.5.0
  (`git diff 3ce9622b..v3.5.0 -- test/smooth_test.cpp`). Not
  consumer-facing churn.
- `manifold-tests.wasm` size: 1.07 MB → 1.16 MB (still under the
  1.40 MB budget; growth is the new ExecutionContext infrastructure).
- `manifold-link-probe.wasm` size: 339 KB → 391 KB (under the
  450 KB budget; same cause).

### Notes for downstream consumers

- Callers using `wasm_cxx_shim_add_manifold()` with defaults will be
  on `v3.5.0` automatically. Override with `MANIFOLD_GIT_TAG <ref>`
  if you need to stay on a v3.4.x pin.
- The `<memory>` stub at `test/manifold-link/include/memory` is
  consumer-copyable (alongside the existing `<mutex>` stub) for
  out-of-tree integrations. The upstream-friendly fix is a
  `MANIFOLD_NO_THREADS` flag on manifold that gates the
  atomic_*(&ctx_) usage; until then, the stub.

## v0.4.0 — manifold #1690 merged upstream, carry-patch dropped (2026-05-04)

Manifold's `MANIFOLD_NO_IOSTREAM` build option is now upstream
(merged in [elalish/manifold#1690][pr1690]). The shim drops its
single carry-patch entirely and bumps the manifold pin to a master
commit past the merge. From here, the helper is just a thin wrapper
that sets `MANIFOLD_NO_IOSTREAM=ON` and lets manifold's own option
chain propagate the macros.

[pr1690]: https://github.com/elalish/manifold/pull/1690

### Removed

- `cmake/manifold-patches/0001-manifold-no-iostream.patch` and the
  whole `cmake/manifold-patches/` directory. The vendored #1690 diff
  is no longer needed; manifold owns the option upstream.
- `SKIP_BUILTIN_PATCHES` argument from
  `wasm_cxx_shim_add_manifold()`. There are no builtin patches to
  skip. Callers that were passing it can drop the argument; callers
  using `EXTRA_MANIFOLD_PATCHES` continue to work unchanged.
- The `install(DIRECTORY .../manifold-patches ...)` rule (the
  directory no longer exists).

### Changed

- Default `MANIFOLD_GIT_TAG` bumped to `3ce9622b` (upstream master
  HEAD at release time, includes #1690 + one downstream commit).
- Helper smoke test updated for the new file layout.

## v0.4.0-alpha.1 — manifold pin bump + #1690 carry-patch (2026-05-03)

Pre-release. Pins manifold to a current upstream master commit and
folds the three shim-side iostream patches into a single vendored
diff of [elalish/manifold#1690][pr1690] (the upstream PR that adds
`MANIFOLD_NO_IOSTREAM` natively). Once #1690 lands and the shim's
manifold pin moves past it, the carry-patch drops entirely and a
v0.4.0 (non-alpha) release follows.

[pr1690]: https://github.com/elalish/manifold/pull/1690

### Added

- `cmake/manifold-patches/0001-manifold-no-iostream.patch` — verbatim
  diff of #1690 against the pinned upstream manifold commit. Replaces
  the three previously-shipped patches.
- Six additional manifold test files now run on the shim:
  `boolean_complex_test`, `manifoldc_test`, `smooth_test` (plus the
  existing `boolean_test`, `cross_section_test`, `sdf_test`). Total
  test count: **121** (up from 71).

### Changed

- `wasm_cxx_shim_add_manifold()`: refactored for the post-#1690
  world. Drops the Clipper2 pre-declaration, sets
  `MANIFOLD_NO_IOSTREAM=ON` as a CMake cache var, and lets manifold's
  carry-patched option chain propagate `MANIFOLD_NO_FILESYSTEM` and
  `CLIPPER2_NO_IOSTREAM` as PUBLIC compile defs.
- API: removed `CLIPPER2_GIT_TAG` and `EXTRA_CLIPPER2_PATCHES`
  parameters (manifold owns the Clipper2 declaration; shim no longer
  has a say). `MANIFOLD_GIT_TAG`, `EXTRA_MANIFOLD_PATCHES`, and
  `SKIP_BUILTIN_PATCHES` continue to work.
- Default `MANIFOLD_GIT_TAG` bumped from `v3.4.1` to a master commit
  (`5f95a3ac`) so the carry-patch applies cleanly.
- `manifold_tests_size_budget` raised from 1.10 MB to 1.40 MB to
  accommodate the expanded test set.

### Removed

- The three shim-side iostream patches
  (`0001-clipper2-strip-iostream`, `0002-manifold-ifdef-iostream`,
  `0003-manifold-test-main-ifdef-filesystem`). Replaced by the
  single vendored #1690 diff.

### Notes for downstream consumers

- This is an **alpha pre-release** pinned to a specific manifold
  commit. The carry-patch may be re-rolled if #1690 evolves in
  review (would land as alpha.2, alpha.3, etc.). The non-alpha
  v0.4.0 ships once #1690 merges.
- Consumers calling `wasm_cxx_shim_add_manifold()` without
  overrides will be on the bumped pin automatically. Override
  with `MANIFOLD_GIT_TAG <ref>` if you need to stay on the v0.3.0
  combination — pair with `EXTRA_MANIFOLD_PATCHES` to supply your
  own iostream patches in that case.

## v0.3.0 — CMake integration helper (2026-05-02)

### Added

- `cmake/WasmCxxShimManifold.cmake` — `wasm_cxx_shim_add_manifold()`
  helper that captures the high-change-rate parts of the
  manifold-on-shim integration cocktail (FetchContent pins, three
  carry-patches, manifold + Clipper2 CMake options, base compile
  flags) in one function call. Loaded automatically by
  `find_package(wasm-cxx-shim)`. Installed to
  `${LIBDIR}/cmake/wasm-cxx-shim/` alongside the integration patches.
- `cmake/manifold-patches/` — canonical home for the three
  integration patches. Previously under `test/manifold-link/patches/`;
  moved so the helper resolves them via `${CMAKE_CURRENT_LIST_DIR}/`
  uniformly across source-tree and install-tree consumption.
- `cmake/check-manifold-helper.cmake` — `cmake -P` script that loads
  the helper and asserts the API surface (command registered,
  defaults populated, shipped patches resolve). Wired as the
  `wasm_cxx_shim_helper_api_smoke` ctest entry; runs on the
  lightweight matrix too.
- Shipped `find_package(wasm-cxx-shim)` config now auto-loads the
  helper via the package's optional include — no extra `include()`
  step required at the consumer side.

### Changed

- `test/manifold-link/CMakeLists.txt` refactored to dogfood the
  helper. ~70 lines of inline FetchContent_Declare + patch commands
  + manifold options + base compile flags collapse to a single
  `wasm_cxx_shim_add_manifold()` call. The dir continues to handle
  shim-side specifics (libcxx headers detection + the `-isystem`
  chain pointing at `__config_site` / `<mutex>` stub).
- `test/consumer/external/CMakeLists.txt` now asserts the helper
  command is registered after `find_package`, end-to-end-testing
  the auto-load against the install tree.

### Notes

- **Backwards compatible with v0.2.0 consumers.** The helper is
  additive. Existing consumers that wired up the integration
  manually (manifold-csg's build.rs, the inline cocktail in older
  `test/manifold-link/CMakeLists.txt` shapes) continue to work
  byte-equivalently. Migration to the helper is opt-in.
- The helper supports overrides for arbitrary refs + custom patch
  sets: `MANIFOLD_GIT_TAG`, `CLIPPER2_GIT_TAG`,
  `EXTRA_MANIFOLD_PATCHES`, `EXTRA_CLIPPER2_PATCHES`,
  `SKIP_BUILTIN_PATCHES`. See `cmake/WasmCxxShimManifold.cmake`'s
  docstring.

## v0.2.0 — manifold integration (2026-05-02)

### Added

- Phase 7-B1: manifold v3.4.1's library + C-API link against the shim
  with zero unexpected wasm imports. `test/manifold-link/` ships a
  probe ctest entry plus the carry-patches needed for the link.
- Phase 7-B2: manifold's own GoogleTest-based tests
  (`boolean_test.cpp` + `sdf_test.cpp` + `cross_section_test.cpp`)
  run on the shim under Node — 71/71 passing. Generic
  `tools/wasm-test-harness/` mechanism (registry + log buffer +
  GoogleTest adapter) lets unmodified consumer test sources compile
  against an adapter header.
- libm correctness probes (`test/libm-check/`), negative-link gate
  (`test/negative-link/`), wasm size budgets across all shipped
  wasms.
- CI: new `manifold` job mirroring the lightweight matrix; 8 required
  status checks total.

### Changed

- `libc` adds `strlen` (musl/wasi-libc); transitively pulled by
  manifold's `std::string` use.

## v0.1.0 — first usable release (2026-05-02)

### Added

- Three independently-buildable CMake components: `libc` (dlmalloc +
  musl `mem*`), `libm` (musl/wasi-libc), `libcxx` (C++ ABI + runtime
  stubs).
- Smoke test exercising vector + unordered_map + virtual dispatch +
  libm — produces a 13.5 KB wasm with zero imports.
- Consumer test validating `find_package(wasm-cxx-shim COMPONENTS …)`
  against an installed prefix.
- CI matrix: {Ubuntu, macOS} × {LLVM 20, 21}.
