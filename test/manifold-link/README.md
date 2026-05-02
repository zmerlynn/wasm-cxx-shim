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

| Project | Tag | Why |
|---|---|---|
| manifold  | `v3.4.1`         | Latest stable as of March 2026. |
| Clipper2  | `Clipper2_2.0.1` | What manifold v3.4.1 pulls. |

Bumping these is a deliberate act tracked in git. When upstream
manifold takes the patches we carry below, drop the patch and bump
the pin.

## Carry-patches

Live under `patches/`. Each is a regular `git format-patch`-style file
applied via FetchContent's `PATCH_COMMAND`. Document the upstream PR
(or its absence) in the patch file's header.

Current patches:

- `0001-clipper2-strip-iostream.patch` — strip `<iostream>` references
  from Clipper2's headers. Originally from
  [pca006132's recipe](https://github.com/elalish/manifold/discussions/1046#discussioncomment-11302257).
  Generated against Clipper2 SHA `46f639177...` (the SHA manifold v3.4.1
  pins). Upstream PR: TODO (cleaner form would be a `#ifdef
  CLIPPER2_NO_IOSTREAM` guard rather than commented-out blocks).

(More patches will be added as the build progresses — see "Status" below.)

## Stub headers

`include/mutex` — no-op `std::mutex` / `std::recursive_mutex` /
`std::lock_guard` / `std::scoped_lock` / `std::unique_lock`. Needed
because libc++ gates these behind `_LIBCPP_HAS_THREADS` (which we
turn off in the smoke test's `__config_site`), but manifold's
`utils.h` and `impl.h` still reference them. Stubbing is smaller
than patching manifold to ifdef the mutex usage. With threads off,
all mutex operations are trivially correct as no-ops.

## Status — green

manifold v3.4.1's library + C API bindings compile against the shim,
link with **zero unexpected wasm imports**, and the probe runs under
Node returning a sane triangle count for a boolean-union operation.
Two ctest entries cover this:

- `manifold_link_imports_check` — wasm-objdump assertion that the
  Import section is absent.
- `manifold_link_run` — Node ESM runner, asserts `probe_run()` returns
  a positive integer (the triangle count from
  `union(cube, translate(cube, 0.5, 0.5, 0.5))`; today it's 36).

What it took to get green:

1. **`MANIFOLD_PAR=OFF`** (not `=-1`; v3.4.1 changed STRING to BOOL).
2. **CMake auto-injection fix** in top-level CMakeLists
   (`CMAKE_<LANG>_IMPLICIT_INCLUDE_DIRECTORIES ""` post-`project()`).
   Stops CMake from prepending clang's resource dir as `-isystem`
   before user `-isystem` paths, which was breaking libc++'s
   `<cstddef>` → `<stddef.h>` resolution chain. Broadly useful — not
   manifold-specific.
3. **`patches/0001-clipper2-strip-iostream.patch`** — strips iostream
   `operator<<` / `OutlinePolyPath*` from Clipper2 v2.0.1 headers.
4. **`patches/0002-manifold-ifdef-iostream.patch`** — wraps manifold's
   OBJ I/O in `#ifndef MANIFOLD_NO_IOSTREAM`. Three blocks:
   `FromChars` template, all of `WriteOBJ*`/`ReadOBJ*` in `impl.cpp`,
   and the C-API `manifold_*_obj` bindings in `manifoldc.cpp`.
5. **Stub `include/mutex`** — no-op `std::mutex`/`recursive_mutex`/
   `lock_guard`/`scoped_lock`/`unique_lock` because libc++ gates
   these behind `_LIBCPP_HAS_THREADS`.
6. **`libcxx-extras.cpp`** — provides the libc++ source-file symbols
   that the main libcxx component intentionally doesn't ship:
   `std::nothrow`, `std::__1::__shared_count::~__shared_count`,
   `std::__1::__shared_weak_count::~__shared_weak_count` /
   `__release_weak` / `lock` / `__get_deleter`, `bad_weak_ptr` key
   functions, and `std::align`. Scoped to this test, NOT part of the
   main libcxx component (which stays insulated from `<memory>` /
   `<new>` includes for libc++ version-drift safety).
7. **Clipper2 utilities/tests/examples disabled** via
   `CLIPPER2_TESTS=OFF`, `CLIPPER2_UTILS=OFF`, `CLIPPER2_EXAMPLES=OFF`
   — defaults are ON and pull in `<sys/types.h>`, googletest, etc.
   we don't ship.

## Upstream PR roadmap

The patches in this directory are carry-patches. Each has a clear
upstream-friendly form documented in its preamble:

- **Clipper2** patch: file as `#ifndef CLIPPER2_NO_IOSTREAM` guards
  upstream against AngusJohnson/Clipper2.
- **Manifold** patch: file as `#ifndef MANIFOLD_NO_IOSTREAM` guards
  upstream against elalish/manifold (could be exposed as a CMake
  option `MANIFOLD_WASM_FREESTANDING=ON` that sets all the relevant
  flags together).

Once both upstreams take the patches, the carry-patches drop and we
just bump the pinned versions.

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
