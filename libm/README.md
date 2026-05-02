# libm — math library subset

Provides the libm symbols required by C/C++ consumers on
`wasm32-unknown-unknown`. Built from a vendored subset of musl libm
(via wasi-libc's tree, which carries wasm-specific tweaks).

## Symbols provided

The 15 named functions (originally from pca006132's manifold-on-wasm
experiment, see [`docs/context.md`](../docs/context.md)):

- **trig**: `acos`, `asin`, `atan2`, `cos`, `sin`
- **power**: `pow`
- **numeric**: `fma`, `fmax`, `fmin`, `hypot`
- **log family**: `ilogb`, `log10`, `log2`
- **specialized**: `remquo`, `round`

Plus workhorses needed transitively or commonly:
- `atan`, `ceil`, `copysign`, `exp`, `fabs`, `floor`, `ldexp`, `log`,
  `scalbn`, `sqrt`, `tan`, `trunc`

All double-precision. Single-precision (`*f`) variants are added on
demand.

## Symbols NOT provided

By demand:
- Single-precision (`sinf`, `cosf`, `powf`, etc.) — most wasm consumers
  use double internally; add when a consumer reports needing them.
- `<complex.h>` — out of scope for v0.1.
- Long-double variants (`*l`) — wasm32 has no `long double` distinct
  from `double`.
- Trig hyperbolic, error functions, gamma — add by demand.

Permanently out:
- `errno`-setting on math errors. We compile `-fno-math-errno`
  matching musl's design. If your code depends on math `errno`, file
  an issue.

## Floating-point stability

Compile flags: `-Os -ffp-contract=off -fno-math-errno
-fno-trapping-math`.

`-ffp-contract=off` is the load-bearing one. It prevents the codegen
from folding `mul + add` into a single fused-multiply-add instruction
opportunistically. Without it, results can drift across consumer
compile-flag changes or wasm runtimes. With it, output is bit-identical
regardless of downstream conditions. We're a numerical-stack shim;
leaning toward determinism over a tiny speed win is the right
trade-off.

This is more conservative than wasi-libc's default. If you have a
specific consumer that needs the FMA path, override the flag in your
build — but verify cross-platform numerical stability matters more
than the speed difference for your case.

## Source provenance

All math sources vendored from
[WebAssembly/wasi-libc](https://github.com/WebAssembly/wasi-libc), tree
`libc-top-half/musl/`. wasi-libc's musl tree is musl-with-wasm-tweaks
already applied — most importantly, alternate-rounding paths gated out
of `__rem_pio2.c`. Of our 42 closure `.c` files, only `__rem_pio2.c`
diverges from upstream musl; the other 41 are byte-identical.

Vendored layout:
- `src/musl/` — 42 `.c` files + 5 `*_data.h` data tables
- `src/internal/libm.h`, `src/internal/atomic.h` — from
  `musl/src/internal/`
- `src/internal/atomic_arch.h`, `src/internal/fp_arch.h` — from
  `musl/arch/wasm32/`
- `src/internal/features.h` — from `musl/src/include/features.h`
  (overrides public `<features.h>` with `hidden`/`weak`/`weak_alias`
  macros that musl libm sources rely on)
- `include/math.h`, `include/features.h` — from `musl/include/`,
  the public chain consumers see

Hand-written:
- `src/internal/endian.h` — 5-line shim. Wasm is little-endian per
  WebAssembly Core Spec §2.1.4. Duplicated from `libc/include/endian.h`
  on purpose (libm is independently buildable; can't pull from libc).
- `include/bits/alltypes.h` — minimal stub providing `float_t` and
  `double_t`. Upstream musl generates a much larger file from a
  TYPEDEF-template; we don't need any of the time/sigset/etc machinery.

Licensing: musl ships as MIT under a project-wide LICENSE file
(reproduced as `LICENSES/LICENSE-musl` in this repo). Many files under
`src/musl/` additionally carry per-file copyright headers from
FreeBSD, Sun Microsystems, or Arm Limited — those headers are
preserved verbatim within each `.c` file.

To re-vendor against a newer wasi-libc snapshot:
```sh
WASI_LIBC_REF=<sha-or-tag> ./scripts/fetch-libm-sources.sh
```

## Internal include structure

The libm CMakeLists puts `src/internal/` and `src/musl/` on the
**private** include path **before** the public `include/` path. This
matters: `src/include/features.h` (the override) defines `hidden`,
`weak`, `weak_alias` — without it, `libm.h` fails to compile because
it uses those macros on every internal extern declaration.

The `BEFORE PRIVATE` keyword in `target_include_directories` forces
this order. Don't reorder without understanding why.

## What this depends on

- A working `<math.h>` (provided here, on the libm INTERFACE include
  path)
- Clang's freestanding `<stdint.h>` and `<float.h>`
- No symbols from libc — libm and libc are independent at the CMake
  level. A consumer that satisfies math from another source can drop
  this whole component without touching libc/libcxx.

## Component independence

- No `target_link_libraries(libm libc)` — the components are
  independent. `endian.h` is duplicated rather than shared from libc
  for the same reason.

## Replacing this component

CMake target: `wasm-cxx-shim::libm`. Drop it
(`-DWASM_CXX_SHIM_BUILD_LIBM=OFF`, or don't add the subdirectory) and
provide your own libm. The libc and libcxx components are unaffected.

A future scenario: someone publishes a smaller or faster wasm-native
libm. At that point this component becomes redundant. Or, if you have
a Rust project that already pulls the
[`libm`](https://crates.io/crates/libm) crate, FFI-export it under the
expected C names and skip this component.
