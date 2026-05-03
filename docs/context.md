# Context: why this exists, what's known, where to start

This document captures the design discussion that led to scaffolding this
repo. It's not a plan — see [`plan.md`](plan.md) for the implementation
roadmap and decision log. This file is the prior art and the reasoning,
so whoever picks up implementation has the same context the author did.

## The gap, in one paragraph

Rust's `wasm32-unknown-unknown` target is the canonical wasm target for
browser-deployed Rust apps because that's what `wasm-bindgen` works with.
But it ships with no libc, no libc++, no libc++abi — so any Rust crate
that wraps a non-trivial C++ library cannot be linked against it. The two
existing solutions (WASI SDK and Emscripten) bind you to a different
target triple — `wasm32-wasi` or `wasm32-unknown-emscripten` —
neither of which is consumable by `wasm-bindgen`. There is no
"freestanding" libc/libc++ package for the bare `wasm32-unknown-unknown`
target. This repo is an attempt to provide one, scoped initially to what
the [manifold-csg](https://github.com/zmerlynn/manifold-csg) Rust
bindings to the [manifold3d](https://github.com/elalish/manifold) C++
CSG kernel actually need.

## The known shopping list

From [pca006132's experiment in
elalish/manifold discussion #1046](https://github.com/elalish/manifold/discussions/1046)
(November 2024), 31 undefined symbols when building manifold3d against
`wasm32-unknown-unknown` with `clang --target=wasm32-unknown-unknown
-fno-exceptions -fno-rtti -nostdlib`, after pulling
[mono-wasm-libc](https://github.com/dotnet/runtime) and a libc++ source
subset:

**Math (15)** — all libm, all double-precision:
- `acos`, `asin`, `atan2`, `cos`, `sin`, `pow`
- `fma`, `fmax`, `fmin`, `hypot`
- `ilogb`, `log10`, `log2`
- `remquo`, `round`

**Memory (5)** — libc allocator + mem ops:
- `malloc`, `free`
- `memcpy`, `memmove`, `memset`

**C++ runtime (11)** — libc++abi + libc++:
- `__cxa_atexit`, `__cxa_pure_virtual`
- 4 × `operator new`: `(unsigned long)`, `[](unsigned long)`,
  `(unsigned long, std::align_val_t)`,
  `(unsigned long, std::nothrow_t const&)`
- 3 × `operator delete`: `(void*)`, `[](void*)`,
  `(void*, std::align_val_t)`
- `std::__1::__libcpp_verbose_abort(char const*, ...)`
- `std::exception::~exception()`

The implementation pass should treat this as the **minimum** set, not
the complete set. Real consumers will surface a few more symbols once
they actually try to link.

## Categories and where each comes from

- **Math (libm)**: Either musl libm (each function ~50 lines of C, MIT
  license) or Rust's
  [`libm` crate](https://crates.io/crates/libm) FFI-exported.
- **Memory (libc)**: Allocator from
  [dlmalloc](http://gee.cs.oswego.edu/dl/html/malloc.html) (CC0) or
  [walloc](https://github.com/wingo/walloc) (MIT). `mem*` from musl.
- **C++ runtime**: Hand-written stubs. Most are 5 lines. The tricky bits
  are operator new alignment handling and getting C++ name mangling
  right for `std::exception::~exception` and friends.

## Why three independently-buildable components

Splitting the repo into `libc/`, `libm/`, `libcxx/` (each its own
CMakeLists.txt with no required dependencies on the others) is so that a
better external solution to any one of them can be swapped in without
disturbing the others. Specifically:

- If LLVM ever ships an actual freestanding libc++ port, our `libcxx/`
  component becomes redundant and consumers should drop it for the real
  thing.
- If a serious wasm-specific allocator project takes off, our libc's
  malloc could be swapped out.
- If the Rust `libm` crate becomes the obvious answer for FFI math, our
  `libm/` could be replaced.

The inverse also matters: a downstream consumer that *only* needs libm
(say, a numeric Rust crate that doesn't use C++) can pull just `libm/`
without the C++ baggage.

The intentional design constraint: **no inter-component CMake
dependencies**. operator new in `libcxx/` needs malloc to exist, but
that requirement is documented in the libcxx README rather than
expressed as a CMake `target_link_libraries(libcxx libc)`. This keeps
each component swappable in isolation.

## Adjacent / overlapping projects

- **WASI SDK** ([WebAssembly/wasi-libc](https://github.com/WebAssembly/wasi-libc)
  + LLVM): targets `wasm32-wasi`. Complete platform. Pulls WASI imports
  even for code that doesn't use them. Not consumable by wasm-bindgen.
- **Emscripten**: targets `wasm32-unknown-emscripten`. Complete platform
  including JS shims. Not consumable by wasm-bindgen.
  (`manifold-csg`'s emscripten support landed in
  [zmerlynn/manifold-csg#33](https://github.com/zmerlynn/manifold-csg/pull/33).)
- **mono-wasm-libc**: internal to .NET; not packaged for general
  consumption. Used by pca006132 in his experiment.
- **wasm32-unknown-unknown-openbsd-libc** (Rust crate): C only, no
  C++. Smaller scope than this project.
- **walloc**: tiny standalone wasm allocator. Could be the basis for
  our `libc/` allocator subcomponent.
- **LLVM
  [issue #77373](https://github.com/llvm/llvm-project/issues/77373)**:
  freestanding-libc++ effort. Scoped to header-only / `<expected>`-style
  C++23 features. Doesn't address the runtime symbol gap. Not a
  competing solution.
- **Rust ABI fix
  [(blog post, April 2025)](https://blog.rust-lang.org/2025/04/04/c-abi-changes-for-wasm32-unknown-unknown/)**:
  made plain C linkable on `wasm32-unknown-unknown`. Necessary
  precondition for this project, doesn't itself solve C++.
- **Manifold roadmap
  [discussion #1064](https://github.com/elalish/manifold/discussions/1064)**:
  lists "support compiling manifold to wasm-unknown-unknown directly"
  as a goal. Aspirational. This project is one path to fulfilling it.

## Stalled prior attempts

- **rust-skia
  [#1078](https://github.com/rust-skia/rust-skia/issues/1078)**
  ("Revisiting wasm32-unknown-unknown") — open since Dec 2024, "trail
  runs cold." rust-skia ships a JS-bridge workaround in production.
- **manifold #1046** — last comment Dec 21, 2024. pca006132's experiment
  was the closest anyone has come; it was not picked up. No follow-up
  in 17 months.

The strongest evidence the niche is unfilled: pca006132 — the most
qualified person to do this — published a 95%-complete recipe and *no
one took it the last 5%* in over a year.

## Strategy notes

- **License**: this repo is MIT. Sources pulled from musl are MIT
  (compatible). dlmalloc is CC0 (compatible). walloc is MIT
  (compatible). libc++/libc++abi reference patterns are Apache-2.0 with
  LLVM exception (compatible). Where source is taken or adapted from a
  third-party project, original copyright + license header is preserved
  in the file.

- **Maintenance model**: scope grows by demand. A new symbol gets added
  when a real consumer reports the linker complaining about it, not
  proactively for completeness. Documented in the top-level README.

- **First consumer**: `manifold-csg` carries an upstream-manifold
  carry-patch that pulls this repo via CMake `FetchContent` and turns
  on a `MANIFOLD_WASM_FREESTANDING=ON` cmake option. (Patch and
  upstream PR to be drafted; see plan.md.)

- **Bus factor**: the explicit framing as "manifold-csg's first target"
  is intentional. If this project is ever abandoned, downstream forks
  for narrow purposes are easy. Don't oversell as a general-purpose
  runtime.

- **Testing**: a smoke test under `test/smoke/` that builds for
  `wasm32-unknown-unknown` and exercises `vector` + `unordered_map` +
  virtual dtor is the minimum acceptance criterion. CI lane runs that
  test on every push.

## What the implementation pass needs to do

Roughly, in order:

1. Decide allocator (dlmalloc vs. walloc vs. something else) and pull
   the source into `libc/src/`.
2. Pull musl `mem*` sources into `libc/src/`.
3. Pull musl libm sources for the 15 functions into `libm/src/`. Or
   prove out the Rust `libm` crate FFI alternative.
4. Write the C++ runtime stubs in `libcxx/src/`. The 31-symbol list is
   the target.
5. Write the smoke test in `test/smoke/` and make it link.
6. Wire up `find_package(wasm-cxx-shim COMPONENTS …)` support — the
   `cmake/WasmCxxShimConfig.cmake.in` template plus install rules.
7. CI workflow that builds the components for `wasm32-unknown-unknown`
   and runs the smoke test (probably via Node, mirroring how
   `manifold-csg` runs its wasm tests today).
8. Once the smoke test is green, draft the upstream-manifold PR adding
   `MANIFOLD_WASM_FREESTANDING=ON` that consumes this repo via
   `FetchContent`.

A reasonable effort estimate for steps 1-5 is one focused weekend.
Steps 6-8 are another half-weekend. Total: ~3 days of focused work to
get a v0.1 release out and a working manifold-csg-on-`wasm32-unknown-unknown`
demo.

## What the implementation pass should NOT do

- Don't try to be complete. Cover the 31 symbols + whatever the smoke
  test surfaces.
- Don't add CMake dependencies between components — keep them
  swappable.
- Don't ship an exception runtime. Stub `__cxa_throw` as abort if
  needed; document loudly.
- Don't add features beyond what `manifold-csg` needs unless you have
  a specific second consumer in mind.
- Don't position this as a general-purpose freestanding wasm
  toolchain. It's a shim, scoped to demand.

## Open questions

All five questions originally listed here have been resolved during the
v0.1 implementation pass. Recorded for the historical record:

- **Allocator choice** → dlmalloc, via the wasi-libc wrapper pattern.
  walloc was rejected (no `realloc`/`calloc`/`aligned_alloc`).
  See `docs/plan.md` "Decisions made before implementation" + the
  research log entry.
- **Math source** → musl, vendored from wasi-libc's tree (which carries
  wasm-specific tweaks already). 47 files. See `docs/plan.md`.
- **`__libcpp_verbose_abort` signature** → header-free stub TU using
  bare attributes; portable across LLVM 16 → main thanks to the
  Itanium-mangling-is-noexcept-invariant rule. See `libcxx/README.md`.
- **Threading** → off. `_LIBCPP_HAS_THREADS 0` in the smoke test's
  `__config_site` override.
- **`__cxa_throw`** → trap stub in `libcxx/src/cxa.cpp`. Documented in
  `libcxx/README.md`. Real exception runtime is permanently out of
  scope for v0.x.

## References

- [manifold #1046: Rust bindings discussion (with pca006132's experiment)](https://github.com/elalish/manifold/discussions/1046)
- [manifold #1064: Post 3.0 roadmap](https://github.com/elalish/manifold/discussions/1064)
- [rust-skia #1078: Revisiting wasm32-unknown-unknown](https://github.com/rust-skia/rust-skia/issues/1078)
- [manifold-csg PR #33: Emscripten support (the prior step)](https://github.com/zmerlynn/manifold-csg/pull/33)
- [LLVM #77373: freestanding libc++](https://github.com/llvm/llvm-project/issues/77373)
- [Rust 1.89 wasm32-unknown-unknown ABI fix](https://blog.rust-lang.org/2025/04/04/c-abi-changes-for-wasm32-unknown-unknown/)
- [WASI SDK](https://github.com/WebAssembly/wasi-sdk)
- [walloc](https://github.com/wingo/walloc)
- [musl libm](https://git.musl-libc.org/cgit/musl/tree/src/math)
- [dlmalloc](http://gee.cs.oswego.edu/dl/html/malloc.html)
