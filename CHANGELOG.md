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

| Shim version | manifold       | Clipper2                                     | Patches shipped       |
|--------------|----------------|----------------------------------------------|-----------------------|
| v0.3.0       | `v3.4.1`       | `46f639177fe418f9689e8ddb74f08a870c71f5b4`   | 0001 + 0002 + 0003    |
| v0.2.0       | `v3.4.1`       | `46f639177fe418f9689e8ddb74f08a870c71f5b4`   | 0001 + 0002 + 0003 (carried in `test/manifold-link/patches/`; helper not yet present) |

Consumers calling `wasm_cxx_shim_add_manifold()` (introduced in v0.3.0)
with no arguments get the row matching their installed shim version.
Pinning to other refs is supported via `MANIFOLD_GIT_TAG` /
`CLIPPER2_GIT_TAG` / `EXTRA_*_PATCHES` parameters; see
`cmake/WasmCxxShimManifold.cmake`.

When upstream PRs land for the carry-patches, the corresponding patch
drops from a future shim release and the table notes the upstream
version where the macro guard appears natively.

---

## Unreleased

(no changes since v0.3.0)

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
