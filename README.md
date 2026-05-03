# wasm-cxx-shim

[![CI](https://github.com/zmerlynn/wasm-cxx-shim/actions/workflows/ci.yml/badge.svg)](https://github.com/zmerlynn/wasm-cxx-shim/actions/workflows/ci.yml)

A minimal, CMake-installable C and C++ runtime shim for the
`wasm32-unknown-unknown` Rust/Clang target.

`wasm32-unknown-unknown` ships with no libc, no libc++, no libc++abi —
which makes it impossible to link C++ code against it out of the box. WASI
SDK and Emscripten both fill the gap but bind you to their respective
platforms (WASI imports, JS shims). This project provides the smaller,
narrower thing you need to compile a self-contained C++ kernel and link
it against `wasm-bindgen`-style wasm without dragging in either ecosystem.

**Status: v0.3.0.** All three components implemented; CI green on
{Ubuntu, macOS} × {LLVM 20, 21}; the headline `wasm32-unknown-unknown`
property (zero unexpected imports) is asserted on every wasm we ship.
A real-world consumer integration — manifold v3.4.1's library + a
slice of its own GoogleTest-based test suite (71/71 across
`boolean_test`, `sdf_test`, `cross_section_test`) — runs end to end
on top of the shim under Node. v0.3.0 ships the
`wasm_cxx_shim_add_manifold()` CMake helper to streamline the
integration for downstream consumers; `manifold-csg` (Rust bindings)
already builds + runs on `wasm32-unknown-unknown` against the shim.
See the release notes for the full picture.

See [`docs/context.md`](docs/context.md) for the design background and
[`docs/plan.md`](docs/plan.md) for the phased implementation roadmap +
decision log.

## Scope

The first concrete target is unblocking
[manifold-csg](https://github.com/zmerlynn/manifold-csg) (Rust bindings to
the [manifold3d](https://github.com/elalish/manifold) CSG kernel) on
`wasm32-unknown-unknown`. Today, manifold-csg supports
`wasm32-unknown-emscripten`; this project aims to make
`wasm32-unknown-unknown` viable so consumers using `wasm-bindgen` (Bevy,
Leptos, Yew, etc.) can use it directly.

The intent is to be useful beyond manifold-csg over time — any cmake-based
C++ library facing the same gap (rust-skia, embedded-CSG/physics
libraries, etc.) should be able to consume this. But the scope grows by
demand: a symbol gets added when a real consumer reports it missing, not
proactively for completeness.

## Structure

The library is split into three buildable units that map onto the
underlying C/C++ runtime layers, so consumers can mix and match — pull in
only what they need, and replace any one with a better standalone if it
appears.

| Subdirectory | Provides | Replaces / equivalent |
|--------------|----------|------------------------|
| [`libc/`](libc/) | malloc/free, memcpy/memmove/memset | musl libc subset, `walloc` for the allocator side |
| [`libm/`](libm/) | sin, cos, pow, hypot, fma, etc. | musl libm, Rust's `libm` crate |
| [`libcxx/`](libcxx/) | `__cxa_*`, `operator new`/`delete`, `__libcpp_verbose_abort`, virtual destructor stubs | LLVM's libc++ + libc++abi subset |

Each subdirectory has its own `CMakeLists.txt` and is independently
buildable. A consumer can do:

```cmake
find_package(wasm-cxx-shim REQUIRED COMPONENTS libc libm libcxx)
target_link_libraries(myproject PRIVATE
  wasm-cxx-shim::libc
  wasm-cxx-shim::libm
  wasm-cxx-shim::libcxx
)
```

…or pick individual components if they already have a better source for
some of them (e.g., satisfying libm from Rust's `libm` crate via FFI
instead of pulling ours).

## Building manifold against the shim

For the common case — building [manifold](https://github.com/elalish/manifold)
+ Clipper2 against the shim — the package config ships a helper:

```cmake
find_package(wasm-cxx-shim REQUIRED COMPONENTS libc libm libcxx)
wasm_cxx_shim_add_manifold()  # uses the tested-pin defaults

target_link_libraries(my-wasm PRIVATE
    wasm-cxx-shim::libc wasm-cxx-shim::libm wasm-cxx-shim::libcxx
    manifoldc manifold Clipper2
    my-libcxx-extras)  # consumer-provided; see test/manifold-link/
```

The helper does FetchContent for both projects, applies the three
carry-patches (`MANIFOLD_NO_IOSTREAM` / `MANIFOLD_NO_FILESYSTEM` /
`CLIPPER2_NO_IOSTREAM`), and flips manifold + Clipper2's CMake options
to disable the bits the shim doesn't support (Python/JS bindings,
threading, googletest pulls, etc.). The tested-pin combination for
each shim release is documented in [`CHANGELOG.md`](CHANGELOG.md);
overrides for arbitrary refs + custom patch sets are supported via
the helper's named arguments — see
[`cmake/WasmCxxShimManifold.cmake`](cmake/WasmCxxShimManifold.cmake)
for the full API.

The shim's [`test/manifold-link/`](test/manifold-link/) is a worked
example that consumes this helper.

## Why three components, not one

If someone publishes a better libm package tomorrow, you should be able
to drop ours and pick theirs without touching the libc/libcxx halves.
Same for the allocator. Keeping the units small and disjoint makes that
swap cheap.

The boundaries follow the conventional C/C++ runtime layering — libc
underneath libcxx underneath user code — but each layer here is a
**subset** sized to actual demand, not a complete reimplementation.

## Non-goals

- A complete libc / libc++ — only what real consumers need. WASI SDK
  exists for the complete-platform case.
- WASI compatibility — this targets the *no-WASI* `wasm32-unknown-unknown`
  triple specifically. There are no `__wasi_*` imports.
- Threading — no pthreads. Emscripten with `-pthread` exists for that.
- File I/O, sockets, time — out of scope. If your code needs them, you
  want WASI SDK.
- Exceptions — release builds use `-fno-exceptions`. Implicit STL throws
  (`bad_alloc`, `regex_error`) abort.

## License

MIT (see [LICENSE](LICENSE)). Where source is derived from third-party
projects (musl, dlmalloc, libc++/libc++abi reference shims), original
license + attribution is preserved alongside the code.

## Status

v0.2.0. End-to-end:

- libc/libm/libcxx components implemented; compile cleanly to wasm32
  static archives.
- Smoke test (`std::vector` + `std::unordered_map` + virtual dtor + libm)
  links against the three shim archives and runs in Node, returning
  the expected value with zero wasm imports.
- `find_package(wasm-cxx-shim COMPONENTS …)` works against an installed
  prefix (validated by `test/consumer/`).
- **CI** green on Ubuntu + macOS × LLVM 20 + 21, both lightweight build
  and heavyweight manifold integration matrices.
- **manifold v3.4.1** links + its full C-API runs against the shim;
  a slice of its GoogleTest suite (`boolean_test` + `sdf_test` +
  `cross_section_test` = 71/71) passes under a generic
  `wasm-test-harness` mechanism (see `tools/wasm-test-harness/`,
  `test/manifold-link/`, `test/manifold-tests/`).
