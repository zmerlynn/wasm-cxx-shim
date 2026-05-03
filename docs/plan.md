# Implementation plan: wasm-cxx-shim v0.1

This is the implementation roadmap. Background and rationale live in
[`context.md`](context.md); this file is the concrete plan, decision log,
and risk register.

The North Star: a `wasm32-unknown-unknown` `.wasm` produced from a
`std::vector` + `std::unordered_map` + virtual-dtor smoke test, linked
against this repo's three components, **with zero undefined imports**,
running cleanly under Node and producing a known return value.

## Decisions made before implementation

These are settled. Re-open only if implementation reveals one is wrong.

| Topic | Decision | Rationale |
|---|---|---|
| Allocator | **dlmalloc** via the wasi-libc wrapper pattern. | walloc lacks `realloc`/`calloc`/`aligned_alloc`; we need all three (vectors + `std::align_val_t`). dlmalloc has 25 years of production hardening, native `aligned_alloc` (`dlmemalign`), and is what Rust std uses on this same target. ~10 KB compiled at `-Oz`. See [research log §dlmalloc](#research-log). |
| libm source | **wasi-libc's musl tree**, not upstream musl. | wasi-libc's `libc-top-half/musl/src/math/` is musl-with-wasm-tweaks (alternate-rounding paths gated out of `__rem_pio2.c`, etc.). Of our 42 closure files, only `__rem_pio2.c` differs from upstream — the other 41 are byte-identical. Lower risk to take their tree. |
| `<math.h>` provisioning | Vendor musl's `<math.h>` + `<bits/alltypes.h>` + tiny `<features.h>` into `libm/include/`, exposed on the libm target's `INTERFACE` include path. | clang freestanding doesn't ship `<math.h>`. Without it, our own libm `.c` files won't even compile. Consumers also need this header to call our functions. Goes on the libm component since libm is the natural owner. |
| libcxx scope | **Only the 11 ABI/runtime stubs** (the 31-symbol list minus libc and libm). | Libc++ source files (`bind.cpp`, `hash.cpp`, `memory.cpp`, `new_helpers.cpp`) that pca006132 compiled belong to the *consumer's libc++ install*. Vendoring them would couple us to a specific LLVM version. We ship only the layer that LLVM does not ship. |
| Exceptions | **Stub `__cxa_throw` as `__builtin_trap()`.** No real unwinder. | Per `context.md`. Consumers compile `-fno-exceptions`. Implicit STL throws (`bad_alloc`, etc.) become crashes, which is correct on wasm. |
| Threading | **Off.** No pthreads, no `_LIBCPP_HAS_NO_THREADS=0` paths. | Per `context.md`. Threaded variant is separate scope. |
| Toolchain for dev/CI | **Bring-your-own clang** with wasm32 target + `wasm-ld`. Default discovery prefers `${WASM_CXX_SHIM_CLANG}`, then a homebrewed `llvm@N` install with bundled `lld`, then the emscripten-bundled LLVM (`/opt/homebrew/Cellar/emscripten/*/libexec/llvm/bin`). | Apple clang has no wasm32 target. We don't want to vendor a toolchain. Most realistic Mac dev environment already has emscripten installed for related work, and that ships a complete LLVM 23 toolchain. CI uses upstream LLVM via apt/brew. |
| Test runner | **Node v20+** (Wasm SIMD + BigInt mature, no flags needed). | `manifold-csg` already runs its emscripten wasm tests in Node. Mirrors the consumer story. |

## Component inventory and risk

| Component | Sources | Risk | Mitigation |
|---|---|---|---|
| `libc/` | dlmalloc (1 upstream `.c`, ~6.3k lines, CC0), wasi-libc wrapper (~100 lines, MIT), wasi-libc `sbrk.c` (~30 lines, MIT), 4 musl `mem*` files (~30 lines each, MIT). | Low. dlmalloc cocktail is well-known. Only fragility is the `errno` macro values — we need `ENOMEM`/`EINVAL` defined somewhere. | Define them as literal numerics in our wrapper file (`#define ENOMEM 12 / EINVAL 22`, matching musl). Document in libc/README. |
| `libm/` | 42 `.c` + 5 `.h` from wasi-libc musl tree, 6 internal headers, vendored `<math.h>` chain (~3 small files). | Medium. Cross-file include path is fiddly: `libm.h` lives in `internal/`, data tables expect to be `#include "exp_data.h"` style co-located with the `.c`s, public `<math.h>` lives at a third path. | Layout: `libm/src/musl/` (the 47 musl files), `libm/src/internal/` (libm.h + arch headers + endian shim), `libm/include/` (public `<math.h>` chain). Compile flags include all three on the PRIVATE side; only `include/` is on INTERFACE. |
| `libcxx/` | 4 hand-written `.cpp` files: `cxa.cpp`, `operator_new_delete.cpp`, `verbose_abort.cpp`, `exception.cpp`. ~50–100 lines each. | Medium-high. Two ABI signatures (`__libcpp_verbose_abort`, `std::exception::~exception()`) have version drift; getting them wrong yields a link-time symbol mismatch that's annoying to debug. | Pinned via research (see below). We define stubs in TUs that **do not include** the libc++ headers, so noexcept-mismatch warnings are inert; the Itanium ABI guarantees the mangled names match. |
| Smoke test | One `.cpp` (vector + unordered_map + virtual dtor). Plus a 30-line Node harness. Plus a vendored `__config_site` override (verbatim from pca006132's recipe) so libc++ headers don't drag in threading/filesystem/locale code. | Medium. The `__config_site` is the most fragile piece: future LLVM versions add new flags. v0.1 targets LLVM 18-21. | Smoke test pins LLVM version range in CI. Document upgrade path in test/smoke/README. |
| CMake `find_package` | `cmake/WasmCxxShimConfig.cmake.in` + per-component install rules + an export set per component. | Low. Standard CMake pattern. | Validated by a `test/consumer/` mini-project that does `find_package(wasm-cxx-shim COMPONENTS libc libm libcxx)`. |
| CI | GitHub Actions: install LLVM, configure with toolchain file, build, link smoke test, run via Node, assert no undefined imports. | Low. | Use `wabt`'s `wasm-objdump -j Import` to assert imports list is empty (or at most a known allow-list). |

## Phased delivery

Phases are sized to what's actually verifiable end-to-end. Each phase ends
with a tangible artifact you can check.

### Phase 0 — Toolchain plumbing (DONE — commit `5da61f8`)

**Goal**: `cmake --preset wasm32` configures, all three component targets
build (still empty), `wasm-objdump` prints something for each `.a`.

- [ ] `cmake/toolchain-wasm32.cmake` — sets `CMAKE_SYSTEM_NAME=Generic`,
  `CMAKE_SYSTEM_PROCESSOR=wasm32`, `CMAKE_C_COMPILER_TARGET` &
  `CMAKE_CXX_COMPILER_TARGET` to `wasm32-unknown-unknown`, locates clang
  via `WASM_CXX_SHIM_CLANG` env, then llvm@N homebrew, then emscripten's
  bundled LLVM. Locates `llvm-ar`/`llvm-ranlib` from the same install.
- [ ] `CMakePresets.json` — one preset `wasm32` that uses the toolchain
  file and sets sensible defaults (`-DCMAKE_BUILD_TYPE=MinSizeRel`,
  `-DWASM_CXX_SHIM_BUILD_TESTS=ON`).
- [ ] Top-level `CMakeLists.txt` cleanup: drop the
  `_empty.c`/`_empty.cpp` placeholder hack; use proper `STATIC` libraries
  with `target_sources` driven by per-component file lists.
- [ ] Update top-level CMake's "wrong target" warning to recognize our
  toolchain file's `Generic`/`wasm32` combo as the expected case (no
  warning) and the `Darwin`/`x86_64` etc. case as the warning case.

**Done when**: `cmake --preset wasm32 && cmake --build --preset wasm32`
produces three empty `.a` files in the build dir, no warnings, on a
fresh checkout with only `WASM_CXX_SHIM_CLANG` set (or auto-detected).

### Phase 1 — libc (DONE — commit `a54fc60`)

**Goal**: `wasm-cxx-shim-libc.a` exposes `malloc/free/calloc/realloc/aligned_alloc/posix_memalign/memcpy/memmove/memset/memcmp` as wasm-callable functions, no undefined imports beyond what the shim itself surfaces (allocator may surface `__heap_base`, that's fine — linker-provided).

- [ ] Vendor sources:
  - `libc/src/dlmalloc/malloc.c` ← upstream Doug Lea v2.8.6
    (https://gee.cs.oswego.edu/pub/misc/malloc.c) — preserve the CC0
    header verbatim. Equivalent: wasi-libc's
    `dlmalloc/src/malloc.c`.
  - `libc/src/dlmalloc/dlmalloc.c` ← adapted from wasi-libc's
    `dlmalloc/src/dlmalloc.c`. Carries the `#define` cocktail and
    public re-exports. License header preserved (MIT, WebAssembly
    Community Group).
  - `libc/src/dlmalloc/sbrk.c` ← adapted from wasi-libc's
    `libc-bottom-half/sources/sbrk.c`. Wraps
    `__builtin_wasm_memory_grow`/`__builtin_wasm_memory_size`.
  - `libc/src/musl/memcpy.c`, `memmove.c`, `memset.c`, `memcmp.c` ←
    upstream musl `src/string/`, MIT headers preserved.
- [ ] `#define` cocktail in `dlmalloc.c` (verbatim from wasi-libc):
  `HAVE_MMAP=0`, `MORECORE_CANNOT_TRIM=1`, `ABORT=__builtin_unreachable()`,
  `LACKS_TIME_H=1`, `NO_MALLINFO=1`, `NO_MALLOC_STATS=1`,
  `MALLOC_ALIGNMENT=16`, `USE_DL_PREFIX=1`,
  `DLMALLOC_EXPORT=static inline`, plus `ENOMEM=12`, `EINVAL=22`.
- [ ] `libc/CMakeLists.txt` — point at the vendored sources, add
  `-fno-strict-aliasing -Wno-null-pointer-arithmetic
  -Wno-unused-but-set-variable -Wno-expansion-to-defined` to the
  dlmalloc compile.
- [ ] `libc/README.md` — note that walloc was considered and rejected
  (with rationale link to plan), document the externally-needed
  `__heap_base` symbol (provided by `wasm-ld`).

**Done when**: `wasm-objdump -d build/libc/wasm-cxx-shim-libc.a` shows
the public symbols; `wasm-ld --no-entry --export-all -o /tmp/probe.wasm
build/libc/wasm-cxx-shim-libc.a` produces a wasm with `__heap_base` as
the only undefined import.

### Phase 2 — libm (DONE — commit `4c353e9`)

**Goal**: `wasm-cxx-shim-libm.a` exposes the 15 named functions plus
the workhorses (sqrt, fabs, exp, log, tan, floor, ceil, trunc, copysign,
scalbn, ldexp, atan), all double-precision. Zero undefined imports.

- [ ] Vendor 47 musl files into `libm/src/musl/` from wasi-libc's
  `libc-top-half/musl/src/math/`. (Concrete URL list lives in research
  log; use a small Bash script `scripts/fetch-libm-sources.sh` to make
  the vendoring reproducible.)
- [ ] Vendor 5 internal headers + 1 endian shim into `libm/src/internal/`:
  - `libm.h`, `atomic.h` ← `musl/src/internal/`
  - `atomic_arch.h`, `fp_arch.h` ← `musl/arch/wasm32/`
  - `features.h` ← `musl/src/include/features.h` (the override one)
  - `endian.h` ← 5-line shim, our own (wasm is little-endian)
- [ ] Vendor public `<math.h>` chain into `libm/include/`:
  - `math.h` ← upstream musl `include/math.h`, lightly trimmed for
    long-double removal (wasm32 has `LDBL_MANT_DIG=53`)
  - `bits/alltypes.h` ← carved-down version covering `double_t`,
    `float_t`, `size_t` (fwd decls only; no syscalls/POSIX types)
  - `features.h` ← 5-line public version (`_Noreturn` macro, etc.)
- [ ] `libm/CMakeLists.txt` — file list, PRIVATE include dirs for `src/musl/`
  and `src/internal/`, INTERFACE include dir for `include/`, the wasi-libc
  warning suppression list (15 `-Wno-*` flags), `-fno-trapping-math`.
- [ ] `libm/README.md` — header layout explanation, list of provided
  symbols, replacement instructions.

**Done when**: a tiny `int test_libm() { return (int)sin(1.0); }` C file
compiles against `libm/include/math.h`, links against
`wasm-cxx-shim-libm.a`, produces a wasm with zero undefined imports
(other than possibly the test entry point).

### Phase 3 — libcxx (DONE — commit `ff22470`)

**Goal**: `wasm-cxx-shim-libcxx.a` exposes the 11 baseline symbols plus a
small set of likely-needed siblings. Zero undefined imports beyond
`malloc`/`free`/`memcpy` (provided by libc).

The full ABI signatures and stub bodies are pinned by research — see
"Research log §libcxx-abi". Concretely:

- [ ] `libcxx/src/cxa.cpp` (~30 lines):
  - `int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }`
    — we never run exit handlers on wasm; report success and forget.
  - `extern "C" void __cxa_pure_virtual() { __builtin_trap(); }`
  - `extern "C" void __cxa_throw(void*, void*, void(*)(void*)) { __builtin_trap(); }`
    — abort stub. **Document loudly** in libcxx/README that this
    means any thrown exception is a crash.
  - `extern "C" void __cxa_throw_bad_array_new_length() { __builtin_trap(); }`
    — likely-needed sibling; cheap to include.
  - `extern "C" void* __dso_handle = nullptr;`
    — referenced by `__cxa_atexit` in C++ code that registers static
    destructors.
- [ ] `libcxx/src/operator_new_delete.cpp` (~40 lines): all 7 variants.
  Default new = `malloc` + abort on null. nothrow new = `malloc` + return
  null on null. align_val new = `aligned_alloc(align, size)`. All deletes
  = `free`.
- [ ] `libcxx/src/verbose_abort.cpp` (~10 lines):
  ```cpp
  namespace std { inline namespace __1 {
  [[__noreturn__]]
  __attribute__((__format__(__printf__, 1, 2)))
  void __libcpp_verbose_abort(const char*, ...) noexcept {
      __builtin_abort();
  }
  }}
  ```
  Critically: this file does **not** `#include <__verbose_abort>` so the
  noexcept drift between LLVM 16-21 doesn't generate compile warnings.
  The Itanium ABI mangling for the symbol is identical with or without
  `noexcept`, so it satisfies the link reference from any libc++.
- [ ] `libcxx/src/exception.cpp` (~30 lines): redefines `class exception`
  in unversioned `namespace std` and provides the out-of-line
  `~exception() noexcept {}` and `what()`. Also provides `bad_exception`
  and `bad_alloc`'s key functions to pre-empt the next round of missing
  symbols. The compiler emits vtable + typeinfo automatically as a side
  effect.
- [ ] `libcxx/src/stubs.cpp` — placeholder for the inevitable "smoke
  test surfaces N more symbols" round. Empty for v0.1; growing later.
- [ ] `libcxx/CMakeLists.txt` — `-fno-exceptions -fno-rtti -Os` per the
  existing scaffold, file list points at the four files above.
- [ ] `libcxx/README.md` — symbol list, document the malloc dependency
  on libc, document `__cxa_throw=trap` policy, document drift policy
  for `__libcpp_verbose_abort`.

**Done when**: a tiny C++ probe TU `struct B{virtual ~B()=default;};
struct D:B{}; D d; void* p = ::operator new(8); ::operator delete(p);`
compiled with `-fno-exceptions -fno-rtti` against the consumer's libc++
headers and linked against libc + libcxx produces a wasm with zero
undefined imports.

### Phase 4 — Smoke test (DONE — commit `e55bba1`)

**Goal**: link the three shim archives end-to-end, prove the wasm has
zero imports, prove the smoke runs in Node.

The original Phase 4 plan also called for a `vector + unordered_map`
smoke. That part split off into Phase 4b once it became clear that
libc++'s STL headers transitively need a libc-header tree we hadn't
built — so v0.1 ended up with two phases here.

What landed in Phase 4: a *minimal* smoke that exercises operator
new[]/delete[] (heap array), virtual dispatch, and libm sqrt/sin
*without* including any libc++ STL header. Proves the runtime symbols
all link. Smoke wasm: 8.6 KB, zero imports, returns 54.

**Done when** (achieved): `ctest --preset wasm32` shows both
`smoke_imports_check` and `smoke_run` green.

### Phase 4b — STL smoke through libc++ headers (DONE — commit `af6785a`)

**Goal**: compile a real `<vector>` + `<unordered_map>` + `<cmath>`
through the consumer's libc++ headers, against the same shim archives.

Added in-flight when scoping showed Phase 4 alone wasn't a strong
enough acceptance signal — without libc++ STL headers exercised, we
weren't proving the thing the project exists for.

What this required:

- Vendor a small set of musl-style libc headers into `libc/include/`
  (`stdio.h`, `stdlib.h`, `wchar.h`, `time.h`, `alloca.h`, plus a
  hand-rolled `bits/alltypes.h` and `features.h`). The functions are
  declared but mostly NOT implemented — calling `fopen`, `time`, etc.
  produces a link-time error, which is correct.
- Update the smoke test's `__config_site` override for LLVM 20+'s
  numeric-value config macros (LLVM 19 used defined/undefined; LLVM 20
  switched to `_LIBCPP_HAS_X 0/1` which is `#if`-evaluable). Critical
  flag: `_LIBCPP_HAS_MUSL_LIBC=1` so libc++ takes the musl-style
  `__NEED_mbstate_t` path that matches our headers.
- Stub `std::__1::__next_prime` in `libcxx/src/stubs.cpp` (~15 lines,
  trial-division). Used by `unordered_map` for hash-bucket sizing.

The original plan called for compiling 4 libc++ TUs from the consumer's
LLVM source tree (`bind.cpp`, `hash.cpp`, `memory.cpp`, `new_helpers.cpp`)
per pca006132's recipe. We **didn't** do that. Reasons:

- Homebrew/apt LLVM packages don't ship libc++ source; would require a
  `FetchContent_Declare(llvm-project ...)` against a 100+ MB tree.
- The 4 files transitively pull in more libc++ source files (recursive).
- We only actually needed `__next_prime` for the smoke; stubbed.

**Trade-off**: a downstream consumer using `std::bind` placeholders,
`std::shared_ptr` atomics, or `std::nothrow` (the global, not just the
type) will hit new missing symbols at link time. Those are surfaced by
their build, not by ours. Documented in `libcxx/README.md`.

Smoke wasm after 4b: 13.5 KB, zero imports, returns 109.

**Done when** (achieved): smoke.cpp is the canonical
vector + unordered_map + virtual dtor + libm version, ctest green.

### Phase 5 — `find_package` install rules + consumer test (DONE — commit `901d750`)

**Goal**: A downstream `cmake` project does
`find_package(wasm-cxx-shim REQUIRED COMPONENTS libc libm libcxx)` and
gets working targets.

- [ ] `cmake/WasmCxxShimConfig.cmake.in` — standard config-mode
  template with `CMAKE_FIND_PACKAGE_HANDLE_STANDARD_ARGS` and per-component
  `_FOUND` variables.
- [ ] Per-component `install(TARGETS …)` + `install(EXPORT …)` rules,
  one export set per component. Component names: `libc`, `libm`,
  `libcxx`.
- [ ] `install(DIRECTORY libm/include/ …)` for the math headers.
- [ ] `test/consumer/` — tiny external CMake project (built in CI as a
  separate step) that does `find_package` and links a no-op TU.
  Verifies the install actually works end-to-end.

**Done when**: CI's "consumer" job builds a `find_package`-driven
project against a freshly-installed shim and links cleanly.

### Phase 6 — CI (NEXT)

**Goal**: Every push to `main` runs the full build + smoke test.
Every PR against `main` runs the same.

- [ ] `.github/workflows/ci.yml`:
  - Matrix: `{ubuntu-latest, macos-latest}` × `{LLVM 18, 19, 20, 21}`.
  - Install LLVM via apt (`llvm.org/apt`) on Ubuntu, `brew install
    llvm@N` on macOS.
  - `wabt` via apt/brew for `wasm-objdump`.
  - Steps: `cmake --preset wasm32 -DLLVM_VERSION=$LLVM_VERSION`,
    `cmake --build --preset wasm32`, `ctest --preset wasm32 --output-on-failure`.
  - Separate "consumer" job: install the shim, then build
    `test/consumer/` against the install.
- [ ] Status badge in top-level README.

**Done when**: CI is green on all four LLVM versions on both OSes.

### Phase 7 — Downstream consumer integration (in flight, post-v0.1)

Originally framed as "manifold-csg integration." During implementation
this naturally split into sub-phases. Sub-phase shorthand:

- **7-B1**: link upstream manifold (the C++ library + C bindings)
  against the shim. Validates the runtime symbol set is sufficient
  to BUILD a real-world consumer.
- **7-B2**: run upstream manifold's TESTS on the shim. Validates
  end-to-end correctness via the consumer's own test suite. Requires
  a generic test-harness mechanism.
- **7-A**: integrate manifold-csg (the Rust crate) on top of #1+#2.
  The Rust-binding-via-bindgen-via-cmake-FetchContent layer.

Status:

- **Phase 7-B1 — DONE (PR #3)**. manifold v3.4.1's
  library + manifoldc compile + link against the shim. Probe runs
  manifold_cube → translate → boolean(ADD); 0 imports; returns 36.
  Two new ctest entries (manifold_link_imports_check, manifold_link_run).
  Two carry-patches at this point (Clipper2 iostream strip, manifold
  OBJ-I/O ifdef) plus test/manifold-link/include/mutex stub and
  libcxx-extras.cpp for shared_ptr machinery + std::nothrow that the
  main libcxx component doesn't ship.
- **Phase 7-B2 — DONE (PR #4)**. Generic test-harness under
  `tools/wasm-test-harness/` plus a GoogleTest translation adapter.
  First consumer integration: `test/manifold-tests/` runs manifold's
  `boolean_test.cpp` (and manifold's own `test/test_main.cpp` —
  helpers + Options global, with main()+filesystem fixture I/O
  ifdef'd out via patches/0003-manifold-test-main-ifdef-filesystem).
  47/47 tests pass at v0.2.0. Coverage extended post-v0.2.0 to also
  include `sdf_test.cpp` (9 tests, libm-heavy) and
  `cross_section_test.cpp` (15 tests, 2D Clipper2 path) — all 71
  passing, no new symbol surface needed. The harness mechanism
  generalizes: future consumers (using GoogleTest, Catch2, doctest,
  or a custom framework) get a small adapter header under
  `tools/wasm-test-harness/adapters/`. Realizes the "run a portion
  of consumer CI in ours" framing.
- **Phase 7-A — not started**. Begins after 7-B1+B2 land.

CI integration (running 7-B1/7-B2 jobs in CI) is a follow-up PR after
the merges; the local ctest is green but the heavyweight build
(manifold+Clipper2 fetch+compile, ~5 min/cell) is only opted-in
locally for now.

## File-tree shape after v0.1

```
.
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE                          (MIT)
├── README.md
├── cmake/
│   ├── toolchain-wasm32.cmake
│   ├── WasmCxxShimConfig.cmake.in
│   └── modules/                     (helper macros if any)
├── docs/
│   ├── context.md
│   └── plan.md                      (this file)
├── libc/
│   ├── CMakeLists.txt
│   ├── README.md
│   └── src/
│       ├── dlmalloc/
│       │   ├── malloc.c             (CC0, upstream verbatim)
│       │   ├── dlmalloc.c           (MIT, wasi-libc-derived)
│       │   └── sbrk.c               (MIT, wasi-libc-derived)
│       └── musl/
│           ├── memcpy.c             (MIT, musl)
│           ├── memmove.c            (MIT, musl)
│           ├── memset.c             (MIT, musl)
│           └── memcmp.c             (MIT, musl)
├── libm/
│   ├── CMakeLists.txt
│   ├── README.md
│   ├── include/
│   │   ├── math.h                   (MIT, musl-derived)
│   │   ├── features.h               (MIT, musl-derived)
│   │   └── bits/
│   │       └── alltypes.h           (MIT, musl-derived)
│   └── src/
│       ├── musl/                    (47 files: 42 .c + 5 _data.h)
│       └── internal/
│           ├── libm.h               (MIT, musl)
│           ├── atomic.h             (MIT, musl)
│           ├── atomic_arch.h        (MIT, musl arch/wasm32/)
│           ├── fp_arch.h            (MIT, musl arch/wasm32/)
│           ├── features.h           (MIT, musl src/include/)
│           └── endian.h             (5-line shim, ours)
├── libcxx/
│   ├── CMakeLists.txt
│   ├── README.md
│   └── src/
│       ├── cxa.cpp                  (ours, ~30 lines)
│       ├── operator_new_delete.cpp  (ours, ~40 lines)
│       ├── verbose_abort.cpp        (ours, ~10 lines)
│       ├── exception.cpp            (ours, ~30 lines)
│       └── stubs.cpp                (placeholder for follow-on adds)
├── scripts/
│   └── fetch-libm-sources.sh        (reproducible vendoring)
├── test/
│   ├── smoke/
│   │   ├── CMakeLists.txt
│   │   ├── README.md
│   │   ├── smoke.cpp
│   │   ├── run.mjs
│   │   └── include/
│   │       ├── __config_site
│   │       └── __assertion_handler
│   └── consumer/                    (mini-cmake project for find_package check)
└── .github/
    └── workflows/
        └── ci.yml
```

## Risk register

| ID | Risk | Likelihood | Severity | Mitigation |
|---|---|---|---|---|
| R1 | `__libcpp_verbose_abort` symbol mismatches future libc++ (LLVM 22+) | Med | Med | TUs don't include libc++ headers; symbol mangling is `noexcept`-invariant on Itanium. CI matrix already covers LLVM 18-21. Tracking script monitors libc++ `__verbose_abort` header diffs. |
| R2 | smoke test surfaces 5+ unexpected symbols | High | Low | Expected; that's why `stubs.cpp` exists. v0.1 scope explicitly says the 31 are the *minimum*, not the *complete* set. |
| R3 | `<math.h>` interaction with consumer's libc++ `<cmath>` | Med | High | We test the smoke path which uses libc++ `<cmath>` → our `<math.h>` chain. If libc++ `<cmath>` requires functions we don't ship (e.g., `expm1`), we add them on demand. |
| R4 | dlmalloc `errno` dependency surfaces as undefined symbol | Low | Low | We define `ENOMEM`/`EINVAL` as macros pre-`#include "malloc.c"`; never reference a real `errno` global. |
| R5 | Static-variable initialization on wasm doesn't run our `__cxa_atexit` registrations | High | Low | We never run them anyway (wasm doesn't have process exit). The shim's `__cxa_atexit` returns 0 (success) and forgets. Static destructors don't run. Document. |
| R6 | wasm-ld version mismatches LLVM and rejects newer .o files | Low | Med | Toolchain file matches `wasm-ld` to the same LLVM install as `clang`. Refuse mixed setups in CMake. |
| R7 | The 4 libc++ TUs (`bind/hash/memory/new_helpers.cpp`) the smoke test compiles introduce more undefined symbols on newer LLVM | Med | Low | Stubs grow as needed. Smoke test is allowed to grow `stubs.cpp` content. |
| R8 | Consumer's libc++ headers not where our `find_package` expects | High on weird setups | Low | The smoke test detects libc++ headers in CMake; downstream consumers do the same in their own build. We don't try to ship headers. |
| R9 | wasm-bindgen ABI compatibility (Rust 1.89+ requirement) | Low | High | This project doesn't use wasm-bindgen directly; consumers do. We just produce vanilla wasm. The Rust 1.89 ABI fix from April 2025 is already shipped; current Rust toolchain 1.95 has it. Document required-min-rust-version (1.89) in README. |

## Research log

This section captures the load-bearing research findings from the
discovery pass, so future implementation work can find the exact
upstream sources and signatures without re-doing the search.

### pca006132 (manifold #1046)

- **Source**: GitHub discussion comment 2024-11-20 by `@pca006132` in
  https://github.com/elalish/manifold/discussions/1046#discussioncomment-11302257
- **Working branch**: `https://github.com/pca006132/manifold/tree/wasm32-unknown-unknown`,
  head SHA `85a4a9b343c5bfd8f2d0699f1b627389676fed69`. Two manifold-side
  patches: one strips `dynamic_pointer_cast` for `-fno-rtti`
  compatibility, the other adds a no-thread `ConcurrentSharedPtr`
  template under `_LIBCPP_HAS_NO_THREADS`. The original
  `elalish/manifold@wasm32-unknown-unknown` branch has been deleted; this
  fork is the only surviving artifact.
- **Build invocation** (verbatim, used as the spec for our smoke test):
  `clang -DCLIPPER2_MAX_DECIMAL_PRECISION=8 -DMANIFOLD_EXPORTS
  -DMANIFOLD_CROSS_SECTION -DMANIFOLD_PAR=-1 ${INCLUDES} -DNDEBUG
  -std=c++17 -Wall -Wno-unused -Werror --target=wasm32-unknown-unknown
  -fno-exceptions -fno-rtti -nostdlib -o out.o -c $f`. Linker:
  `wasm-ld --no-entry --export-all --warn-unresolved-symbols *.o
  -o libmanifold.wasm`.
- **libc**: he used `migueldeicaza/mono-wasm-libc` (musl fork)
  *headers only*, no `.c` files compiled. Our shim provides the actual
  symbols dlmalloc + musl `mem*`.
- **libc++**: he compiled exactly 4 TUs from `llvm-project/libcxx/src/`:
  `bind.cpp`, `hash.cpp`, `memory.cpp`, `new_helpers.cpp`. He overrode
  `__config_site` and `__assertion_handler` with custom files (verbatim
  reproduced in research dump; these go into `test/smoke/include/`).
- **He never linked-and-ran**. The 31-symbol list is where he stopped.
  Rest of the work (writing stubs, getting a working binary, validating
  static init) is what this project does.
- **Allocator**: he treated `malloc`/`free` as undefined imports to be
  resolved by the embedder. Our project resolves them in-wasm via
  dlmalloc.

### libc++ ABI signatures

- **`__libcpp_verbose_abort`**:
  - Header: `libcxx/include/__verbose_abort` in `llvm/llvm-project`
  - Drift: noreturn always present; `noexcept` added in LLVM 20
    (opt-out via `-D_LIBCPP_VERBOSE_ABORT_NOT_NOEXCEPT`), made
    unconditional in LLVM 21+.
  - Mangled: `_ZNSt3__122__libcpp_verbose_abortEPKcz` (versioned
    namespace `std::__1::`). `noexcept` is **not** part of the
    Itanium mangling, so a stub without `noexcept` still satisfies a
    LLVM 21 reference.
  - Drop-in stub: see Phase 3 above.

- **`std::exception::~exception()`**:
  - Header: `libcxx/include/__exception/exception.h`. Lives in the
    **unversioned** namespace (`_LIBCPP_BEGIN_UNVERSIONED_NAMESPACE_STD`,
    i.e., plain `std::`, not `std::__1::`).
  - Out-of-line def: `libcxxabi/src/stdlib_exception.cpp`. Stable
    across LLVM 16-main.
  - Mangled: `_ZNSt9exceptionD2Ev` (base), `_ZNSt9exceptionD1Ev`
    (complete), `_ZNSt9exceptionD0Ev` (deleting). NOT
    `_ZNSt3__19exceptionD2Ev` — the versioned namespace would mangle
    differently.
  - The compiler emits vtable (`_ZTVSt9exception`) and typeinfo
    (`_ZTISt9exception`/`_ZTSSt9exception`) automatically alongside
    the out-of-line dtor (Itanium "key function" rule).
  - Drop-in stub: see Phase 3 above. Don't `#include <exception>` in
    the stub TU; redefine the class locally.

### musl libm closure

- **42 `.c` + 5 `.h` under `src/math/`** + 6 internal headers + endian
  shim + 3 public-header files. Full URL list: every file at
  `https://raw.githubusercontent.com/WebAssembly/wasi-libc/main/libc-top-half/musl/src/math/<file>`.
- **wasi-libc tree, not upstream musl**: of our 42 closure `.c` files,
  41 are byte-identical between the two; `__rem_pio2.c` is the only
  edited one (alternate-rounding paths gated out via
  `__wasilibc_unmodified_upstream`). Take wasi-libc's tree to inherit
  that fix.
- **NOT in closure** (single-precision and exp2/cosh/sinh helpers):
  `__expo2.c`, `__expo2f.c`, `__sindf.c`, `__cosdf.c`, `__tandf.c`,
  `__rem_pio2f.c`. Not needed for our 15 double-precision seeds + the
  workhorses listed.
- **Compile flags**: 15 `-Wno-*` flags from wasi-libc's
  `libc-top-half/CMakeLists.txt` (silences musl's deliberate `&|` precedence,
  unused vars, etc.); plus `-fno-trapping-math` (matches wasi-libc;
  redundant on wasm but harmless); plus existing `-Os -fno-math-errno`.
- **`<endian.h>`**: clang doesn't ship one; libm's internal `libm.h`
  needs `__BYTE_ORDER`. We write a 5-line shim (wasm is little-endian
  per spec §2.1.4).

### dlmalloc cocktail

- **Source**: 1 upstream `.c` (Doug Lea's `malloc.c` v2.8.6, 6366 lines,
  CC0). Wasi-libc supplies a thin `dlmalloc.c` wrapper file (~100 lines,
  MIT) that `#include`s it with the right `#define`s, plus a `sbrk.c`
  (~30 lines, MIT) that wraps wasm `memory.grow`/`memory.size`.
- **Cocktail of `#define`s**: `HAVE_MMAP=0`, `MORECORE_CANNOT_TRIM=1`,
  `ABORT=__builtin_unreachable()`, `LACKS_TIME_H=1`, `NO_MALLINFO=1`,
  `NO_MALLOC_STATS=1`, `MALLOC_ALIGNMENT=16`, `USE_DL_PREFIX=1`,
  `DLMALLOC_EXPORT=static inline`. We add `ENOMEM=12`, `EINVAL=22` for
  errno macros.
- **Public re-exports**: `malloc`, `free`, `calloc`, `realloc`,
  `aligned_alloc` (alias to `dlmemalign`), `posix_memalign`,
  `malloc_usable_size`. Public `aligned_alloc` is the one the libcxx
  `operator new(size_t, std::align_val_t)` needs.
- **License audit**: dlmalloc CC0, walloc MIT (not BSD-2 as
  `context.md` says — context.md needs a fix), wasi-libc wrappers MIT.
  All compatible with our MIT.
- **Compiled size**: ~6-10 KB at `-Oz` after dead-strip. Negligible
  for manifold-csg's 1-5 MB target wasm.

## Open items deferred past v0.1

These are real but explicitly out of scope:

- **Threaded variant.** Separate scope; needs a different libcxx config
  and pthread shims.
- **A real exception runtime.** Out of scope per `context.md`. v0.x
  could reconsider if a consumer brings clear demand.
- **`<complex.h>` math.** Add when a consumer asks.
- **Single-precision `*f` math functions.** Trivial extensions; add when
  a consumer asks.
- **`errno` global**. dlmalloc uses macros; if a consumer needs the
  global, separate scope.
- **walloc as a size-optimized alternate libc backend.** Possible v0.x
  if binary size becomes a problem.
- **Locale, wide chars, wcsxxx, file I/O.** Permanently out of scope —
  use WASI SDK if you need any of these.

## Effort estimate

| Phase | Estimate | Cumulative |
|---|---|---|
| 0 — toolchain plumbing | 0.5 day | 0.5 |
| 1 — libc | 1 day | 1.5 |
| 2 — libm | 1 day | 2.5 |
| 3 — libcxx | 1 day | 3.5 |
| 4 — smoke test | 0.5 day | 4.0 |
| 5 — find_package + consumer test | 0.5 day | 4.5 |
| 6 — CI | 0.5 day | 5.0 |

That's the same ~3 focused days `context.md` estimated, padded for the
"surface 5 more symbols on first link" phase that always happens. v0.1
tag goal: 5 working days from start of phase 0.

Phase 7 (manifold-csg integration) is post-v0.1, separate effort.
