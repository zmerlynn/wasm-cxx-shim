# test/manifold-link — link upstream manifold against the shim

Phase 7-precursor (B1 in the planning shorthand): prove that the
elalish/manifold C++ library builds + links against the shim with
zero unexpected wasm imports. **Not** a "manifold tests run on wasm"
test — that's B2, which would also need GoogleTest on wasm and is
out of v0.1.x scope.

## What this exercises

- manifold's full C-API library (`manifoldc`) compiled for
  `wasm32-unknown-unknown`.
- Clipper2 (manifold's 2D Boolean dependency) ditto.
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
`MANIFOLD_GIT_TAG`). Clipper2's pin is owned by manifold's own
`cmake/manifoldDeps.cmake`; the shim no longer pre-declares it.

Each shim release verifies a specific manifold pin in CI; see
`CHANGELOG.md`'s "Tested upstream combinations" table for the
mapping.

## Carry-patches

None at v0.4.0+. Manifold's `MANIFOLD_NO_IOSTREAM` build option is
now upstream
([elalish/manifold#1690](https://github.com/elalish/manifold/pull/1690),
merged 2026-05-04). The helper sets the option ON; manifold owns the
rest, including the bundled Clipper2 carry-patch that mirrors
[AngusJohnson/Clipper2#1094](https://github.com/AngusJohnson/Clipper2/pull/1094).

Callers can still pass `EXTRA_MANIFOLD_PATCHES` to apply their own
patches against the pinned manifold checkout if needed.

## Stub headers

`include/mutex` — no-op `std::mutex` / `std::recursive_mutex` /
`std::lock_guard` / `std::scoped_lock` / `std::unique_lock`. Needed
because libc++ gates these behind `_LIBCPP_HAS_THREADS` (which we
turn off in the smoke test's `__config_site`), but manifold's
`utils.h` and `impl.h` still reference them. Stubbing is smaller
than patching manifold to ifdef the mutex usage. With threads off,
all mutex operations are trivially correct as no-ops.

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
   `lock_guard`/`scoped_lock`/`unique_lock` because libc++ gates
   these behind `_LIBCPP_HAS_THREADS`.
5. **`libcxx-extras.cpp`** — provides the libc++ source-file symbols
   that the main libcxx component intentionally doesn't ship:
   `std::nothrow`, `std::__1::__shared_count::~__shared_count`,
   `std::__1::__shared_weak_count::~__shared_weak_count` /
   `__release_weak` / `lock` / `__get_deleter`, `bad_weak_ptr` key
   functions, and `std::align`. Scoped to this test, NOT part of the
   main libcxx component (which stays insulated from `<memory>` /
   `<new>` includes for libc++ version-drift safety).
6. **Clipper2 utilities/tests/examples disabled** via
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

Default is `OFF` — the FetchContent clone of manifold + Clipper2 +
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
