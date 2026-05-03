---
name: review
description: Run a thorough code review of wasm-cxx-shim, focused on wasm linkability, C++ ABI correctness, vendored-source hygiene, and component independence
user-invocable: true
---

# Review — wasm-cxx-shim

Run a thorough code review of the wasm-cxx-shim repo. The project's
risk profile is unusual: the most common bug class is "the shim links
fine but the resulting `.wasm` has unexpected imports or undefined
symbols at instantiation time." Most categories below target that risk
class.

Read [`CLAUDE.md`](../../../CLAUDE.md) first if you haven't — the hard
constraints, vendoring rules, and ABI mangling reference are the
ground truth this review checks against.

## Arguments

- No arguments: review every hand-written source file under `libc/`,
  `libm/`, `libcxx/`, `test/`, `cmake/`, plus root-level CMake and docs.
  **Skip vendored code in `libc/src/dlmalloc/malloc.c`,
  `libc/src/musl/`, `libm/src/musl/`, `libm/src/internal/`,
  `libm/include/` (the musl-derived `<math.h>` chain).** Touching those
  is reviewed as a *vendoring* change (Category 4), not as a code
  review.
- A file path or glob: review only matching files.
- `staged`: review only staged files (`git diff --cached --name-only`).
- `branch`: review only files changed on the current branch vs `main`
  (`git diff --name-only main...HEAD`).
- `since <ref>`: review only files changed since a ref (e.g.,
  `since v0.1.0`).

## Scope router

Pick categories to run based on what's changed. Always include
Category 1 (wasm linkability).

| Changed paths | Categories to run |
|---|---|
| `libc/src/dlmalloc/`, `libc/src/musl/` (vendored) | 4, then 1 (verify the wasm produced still has zero imports) |
| `libc/src/dlmalloc/dlmalloc.c` (our wrapper) | 1, 2, 5, 7 |
| `libm/src/musl/`, `libm/src/internal/`, `libm/include/` (vendored) | 4, then 1 |
| `libm/CMakeLists.txt` | 5, 6 |
| `libcxx/src/*.cpp` | 1, 2, 3, 7 |
| `test/smoke/` | 1, 7, 8 |
| `test/consumer/` | 6 |
| `cmake/`, top-level `CMakeLists.txt`, `CMakePresets.json` | 5, 6 |
| `docs/`, `README.md`, component `README.md` | 8, 9 |
| `.github/workflows/` | 6, 10 |

Always check, regardless of which files changed: if a hard constraint
from CLAUDE.md is being relaxed (no inter-component CMake deps, no
WASI imports, no threading, no exception runtime), flag it as
**error** unless the change includes an explicit CLAUDE.md update.

## Review categories

### 1. Wasm linkability

The single most important property of this project: the produced
`.wasm` must have **zero unexpected imports** and **zero undefined
symbols**. Verify, for each changed component:

- **Imports list is clean.** Run `wasm-objdump -j Import
  build/test/smoke/smoke.wasm` (assumes the smoke test build is
  current). The expected import list is empty. Any import that isn't
  `__heap_base` (linker-provided, resolves at link time) is a finding.
  WASI imports (`wasi_snapshot_preview1.*`, `__wasi_*`) are **errors**.
- **Undefined symbols are zero.** `llvm-nm -u
  build/test/smoke/smoke.wasm`. Anything output is a finding.
- **No syscall-style code.** Grep changed C/C++ files for
  `<unistd.h>`, `<sys/`, `<fcntl.h>`, `<dirent.h>`, `clock_gettime`,
  `gettimeofday`, `open(`, `read(`, `write(`, `close(`. Any hit is an
  **error** unless the include is in vendored code that's already
  guarded.
- **Memory growth uses builtins, not sbrk-the-syscall.** Allocator
  code that calls `sbrk` is fine *only* if it resolves to our internal
  `sbrk.c` shim that wraps `__builtin_wasm_memory_grow`. Confirm the
  shim is the resolution (`wasm-objdump -d` should show the wasm
  intrinsic, not an import).
- **`__heap_base` is the only externally-resolved symbol** for the
  allocator. wasm-ld provides it at link time. Anything else
  ("`environ`", "`__assert_fail`", etc.) is a finding.
- **Smoke test export is `run`** (or whatever the CMake target
  declares); no accidental extra exports unless the smoke test asks
  for them via `--export`. Run `wasm-objdump -j Export` to verify.
- **Every new wasm artifact has both `*_imports_check` and
  `*_size_budget` ctest entries.** Pattern is established across
  smoke, libm-check, harness self-test, manifold-link, and
  manifold-tests (since PR #7). A PR adding a new wasm without
  *both* assertions is a finding — the imports-check protects the
  headline "no unexpected imports" property; the size budget
  provides early warning of accidental code-bloat regressions
  (e.g., a new libcxx-extras addition pulling in a stray
  dependency). Reuse `test/smoke/check-imports.sh` and
  `test/smoke/check-size.sh`; budgets are hand-tuned to current
  size + ~15-50% margin.

### 2. C++ ABI correctness (libcxx component)

The libcxx stubs are mangled-name-sensitive. Two specific signatures
have version drift across LLVM releases. Verify:

- **`std::__1::__libcpp_verbose_abort`** lives in the **versioned**
  namespace (`std::__1::`). Mangled `_ZNSt3__122__libcpp_verbose_abortEPKcz`.
  `noexcept` does not affect Itanium mangling, so a stub without
  `noexcept` still satisfies a LLVM 21 reference — but the
  *declaration in the stub TU should not include the libc++ header*
  (`<__verbose_abort>`), or the compiler will emit a noexcept-mismatch
  warning on LLVM 20+. If you see `#include <__verbose_abort>` in any
  libcxx stub TU, it's a **warning**.
- **`std::exception::~exception()`** lives in the **unversioned**
  namespace (plain `std::`, not `std::__1::`). Mangled
  `_ZNSt9exceptionD2Ev` / `D1Ev` / `D0Ev`. If the mangling shows
  `_ZNSt3__19exception...`, the stub is in the wrong namespace —
  **error**, link will fail in confusing ways. Confirm by running
  `llvm-nm build/libcxx/libwasm-cxx-shim-libcxx.a | grep exception`
  and checking the symbols.
- **vtable + typeinfo emission.** The Itanium "key function" rule says
  the TU defining `~exception()` out of line emits the vtable
  (`_ZTVSt9exception`) and typeinfo (`_ZTISt9exception`,
  `_ZTSSt9exception`) automatically. Verify they appear in the
  `.a`. If the stub TU is compiled `-fno-rtti` and the consumer also
  uses `-fno-rtti`, the typeinfo can be elided — that's fine. But if
  the consumer might use RTTI, the typeinfo must be present.
- **No `#include <exception>` in the libcxx stub that defines
  `std::exception::~exception()`.** Local re-declaration of the class
  is the documented strategy (CLAUDE.md). `#include`-ing the libc++
  header would re-declare the class with attribute conflicts.
- **`__cxa_atexit` returns 0.** It's a "registered, will run"
  acknowledgement. We never actually run the registered destructors
  (no process exit on wasm). Returning non-zero on failure is fine
  but should never happen in practice.
- **`__cxa_pure_virtual` and `__cxa_throw` use `__builtin_trap()`.**
  Not `abort()` — `abort()` would surface as another import. Trap is
  in-wasm.
- **`__dso_handle` exported as `void*`** (referenced by `__cxa_atexit`
  call sites). Not a function. If a stub defines it as a function, the
  consumer's reference will produce a type-mismatch link error.
- **All seven `operator new`/`operator delete` variants present:**
  4 new (default, array, align_val, nothrow) + 3 delete (default,
  array, align_val). Missing align_val variants are common — verify by
  `llvm-nm libcxx.a | grep -E 'Znw|Znam|Zdl|Zda'`.
- **`operator new(size_t, std::align_val_t)` actually uses
  `aligned_alloc`**, not plain `malloc`. Calling plain malloc with
  `__builtin_alloca`-style alignment ignores the align request and is
  silently broken on `std::align_val_t(32)+` requests.

### 3. C++ runtime semantics (libcxx component)

- **`operator new` aborts on null malloc return** (or traps); the
  `nothrow` variant returns null. Don't conflate.
- **`operator delete` is null-tolerant.** `free(nullptr)` is
  well-defined; the C++ deletes inherit that.
- **`__libcpp_verbose_abort` doesn't recurse.** If the abort handler
  itself calls `printf` or anything that could trigger a libc++
  assertion, you get a recursion stack. Use `__builtin_abort()` or
  `__builtin_trap()` directly.
- **Static destructor order is "never runs."** Document in
  `libcxx/README.md` if not already.
- **Exception runtime is a trap.** Any change that promotes
  `__cxa_throw` to a real implementation is a major scope change
  and needs a CLAUDE.md update.

### 4. Vendored source hygiene

For any change touching `libc/src/dlmalloc/malloc.c`,
`libc/src/musl/`, `libc/include/` (vendored headers), `libm/src/musl/`,
`libm/src/internal/`, or `libm/include/`:

- **Provenance link is present.** Every vendored, cribbed, or
  derived-from-upstream file must EITHER have an upstream URL in its
  top comment OR be listed in the surrounding component's README
  source-provenance table with a URL. A file with neither — looking
  identical to a hand-written file — is a **warning** (a future
  maintainer may modify it, breaking re-vendoring). Per CLAUDE.md
  vendoring rule 2.
- **License header preserved verbatim** at the top of the file (when
  upstream had one). Compare against the upstream URL. Re-formatted,
  removed, or edited license headers are an **error**.
- **Files without per-file headers (musl)**: their LICENSE text must
  be shipped in `LICENSES/` at the repo root. Missing `LICENSES/LICENSE-musl`
  or `LICENSES/LICENSE-dlmalloc` when the corresponding sources are
  vendored is a **warning** (compliance with MIT/musl distribution
  terms).
- **Source URL in component README matches.** `libc/README.md` and
  `libm/README.md` should record the upstream source for every
  vendored file. New vendored files without README entries are a
  **warning**.
- **Modifications wrapped in greppable markers.** `// wasm-cxx-shim:`
  comments at the start and end of any patched region. Untracked
  modifications (silent diff vs upstream) are an **error** —
  re-vendoring will silently drop them.
- **No style cleanup of vendored code.** Renaming, reformatting,
  reflowing comments, etc. inside a vendored file is a **warning**;
  it makes future re-vendoring painful. Surface as "consider reverting
  the cosmetic changes."
- **Use wasi-libc's musl tree for libm, upstream musl for libc public
  headers.**
  wasi-libc's libm tree carries wasm-specific edits we want. But
  wasi-libc's libc public headers (`stdio.h`, `stdlib.h`) pull in
  wasi-bottom-half-specific headers we don't have — for those, fetch
  from upstream musl directly. If new files are pulled from the wrong
  upstream, flag for re-vendoring from the right one.
- **License compatibility.** Only MIT, CC0/public-domain,
  Apache-2.0-with-LLVM-exception, BSD-2/BSD-3 are acceptable. Any
  copyleft (GPL/LGPL/AGPL/SSPL/MPL) is an **error**.
- **dlmalloc cocktail integrity.** Compare the `#define`s in
  `libc/src/dlmalloc/dlmalloc.c` against the canonical list in
  CLAUDE.md AND `libc/README.md`. Missing or changed defines, or a
  drifted CLAUDE.md mirror, are a **warning**.
- **Component-internal duplicate files stay in sync.** `libc/include/bits/alltypes.h`
  and `libm/include/bits/alltypes.h` (and `libc/include/features.h`
  vs `libm/include/features.h`, and `libc/include/endian.h` vs
  `libm/src/internal/endian.h`) must have identical functional content
  — whichever copy is found first on the include path wins, so drift
  silently affects link order. A `diff` showing semantic difference
  is a **warning**.

### 5. CMake correctness & component independence

- **No inter-component `target_link_libraries`.** The libc, libm, and
  libcxx targets must not link each other at the CMake level. The
  documented `libcxx → malloc` dependency is *consumer-fulfilled*, not
  CMake-expressed. A `target_link_libraries(wasm-cxx-shim-libcxx ...
  wasm-cxx-shim-libc)` is an **error** — see CLAUDE.md hard constraints.
- **PUBLIC vs INTERFACE include directories** correctly distinguish
  private build-tree paths from headers consumers should see. The
  `libm/include/` directory is `INTERFACE` (or
  `PUBLIC $<BUILD_INTERFACE:...> $<INSTALL_INTERFACE:...>` for
  install-time). The internal `libm/src/internal/` is `PRIVATE`.
- **`add_library(... STATIC)` for all components.** No
  `OBJECT`/`SHARED` libraries — wasm32-unknown-unknown doesn't really
  do shared, and OBJECT libraries lose the export-set abstractions.
- **Target naming.** `wasm-cxx-shim-libc`, `wasm-cxx-shim-libm`,
  `wasm-cxx-shim-libcxx` (the actual build artifacts), with
  `wasm-cxx-shim::libc`, etc., as ALIAS targets for consumer use.
  Don't use `add_library(libc ...)` — too generic; collides.
- **`CMakeLists.txt` placeholders cleaned up.** The initial scaffold
  used `_empty.c`/`_empty.cpp` placeholder files. Once the components
  have real sources, those placeholders should be gone.
- **Sanity-check warning is correctly gated.** Top-level CMake's
  "wrong target" warning should fire on native builds (Darwin/Linux
  on x86_64/aarch64) and stay quiet under our toolchain file
  (`Generic/wasm32`). Mis-gating either way is a **warning**.
- **Compile flags match the documented set.**
  - libc dlmalloc: `-Os -fno-strict-aliasing -Wno-null-pointer-arithmetic
    -Wno-unused-but-set-variable -Wno-expansion-to-defined`.
  - libm: `-Os -fno-math-errno -fno-trapping-math` plus the 15
    `-Wno-*` flags from wasi-libc (verify against the list in
    `docs/plan.md`).
  - libcxx: `-Os -fno-exceptions -fno-rtti`.
- **CMake helper modules capture `CMAKE_CURRENT_LIST_DIR` at module
  load, not in function bodies.** Helpers that resolve shipped data
  (e.g., `cmake/WasmCxxShimManifold.cmake` resolving its sibling
  `manifold-patches/` directory) must `set(_helper_dir "${CMAKE_CURRENT_LIST_DIR}")`
  at the module's top level and reference `${_helper_dir}` inside the
  function. Inside the function, `${CMAKE_CURRENT_LIST_DIR}` resolves
  to the *caller's* CMakeLists path, not the module's — silently
  broken when called from a non-shim CMakeLists. A function that
  uses `${CMAKE_CURRENT_LIST_DIR}` directly to find a sibling file
  is a **warning** (might happen to work today; will break the
  moment the helper is consumed from a different location).
- **`find_package(wasm-cxx-shim COMPONENTS …)` works.** Verified via
  `test/consumer/`. Failure mode: `find_package` succeeds but the
  resulting target has no usable interface (empty include path,
  unset link flags). Test by configuring `test/consumer/` against an
  installed prefix.
- **Install rules cover headers.** `libm/include/` must be installed,
  or downstream consumers can't `#include <math.h>`.
- **`FetchContent_Declare(... PATCH_COMMAND ...)` is idempotent.**
  Re-configures (e.g., from any CMakeLists.txt edit) re-trigger
  PATCH_COMMAND; vanilla `git apply` then fails on the already-patched
  source tree. Acceptable mitigations: `UPDATE_DISCONNECTED TRUE` on
  the FetchContent_Declare (short-circuits the patch step on
  subsequent configures), or a wrapper `cmake -P` script that does
  `git apply --reverse --check` first. A bare `PATCH_COMMAND git
  apply ${PATCH}` with neither mitigation is an **error** — works
  on a clean tree, breaks the moment the user re-configures.
- **Order-of-operations on derived options across nested
  `FetchContent_MakeAvailable` calls.** When a consumer pre-declares
  package A before package B's MakeAvailable runs, B's CMakeLists
  runs after A is already populated. Any cache var B sets to
  influence A's option resolution arrives too late. If a helper has
  a pre-declare, audit whether a transitive dep of the
  pre-declared package needs to consume an option set by an
  intermediate CMakeLists — if so, drop the pre-declare or set the
  cache var explicitly in the helper. **Warning** if a
  consumer-pre-declared dep also has cache-var dependencies coming
  from a nested package.

### 6. Toolchain & build infrastructure

- **`cmake/toolchain-wasm32.cmake` auto-detects clang in the documented
  order**: `${WASM_CXX_SHIM_CLANG}` → homebrew `llvm@N` + `lld@N` →
  emscripten-bundled LLVM. A failed detection should print a clear
  error pointing at the env var to set, not a CMake stack trace.
- **`CMAKE_SYSTEM_NAME=Generic`, `CMAKE_SYSTEM_PROCESSOR=wasm32`** in
  the toolchain file. Setting `WASI` would imply WASI sysroot and is
  wrong for our target.
- **`CMAKE_C_COMPILER_TARGET=wasm32-unknown-unknown`** and ditto for
  CXX. Don't set via `add_compile_options(--target=...)` — using
  `CMAKE_*_COMPILER_TARGET` flows through to `try_compile` and
  feature detection.
- **`CMAKE_AR` and `CMAKE_RANLIB` point at `llvm-ar` /
  `llvm-ranlib`** from the same install as `clang`. Apple `ar` won't
  understand wasm `.o` files.
- **CMakePresets.json** has a `wasm32` configure preset and a matching
  build/test preset. The build preset enables tests
  (`-DWASM_CXX_SHIM_BUILD_TESTS=ON`).
- **CI uses LLVM 18+.** The CI matrix should cover LLVM 18, 19, 20,
  21 (or whichever range is current). Older LLVM is a flag day for
  libc++ ABI; newer LLVM should be added when available.
- **CI runs the smoke test under Node, not just builds.** `ctest
  --output-on-failure` should be a CI step. A green build that didn't
  exercise the smoke runner is a false positive.
- **CI asserts the imports list is empty.** Use `wasm-objdump -j
  Import` and grep for emptiness, or use a small Node script that
  loads the wasm and lists imports. If the assertion is missing, this
  whole project's reason-for-being is unverified.

### 7. Test correctness

- **Smoke test exercises the right shape.** The vector +
  unordered_map + virtual dtor combo is the documented baseline (see
  `test/smoke/README.md` and `docs/plan.md`). Anything narrower is a
  regression.
- **Smoke test asserts a return value, not just exit-0.** A
  smoke that crashes during init can still be exit-0 if Node doesn't
  surface the trap. The runner should explicitly assert the integer
  value of `run()`.
- **Smoke test compiles consumer-side libc++ TUs**
  (`bind.cpp`/`hash.cpp`/`memory.cpp`/`new_helpers.cpp` from the
  user's libc++ source tree, per pca006132's recipe). Without these,
  the link will surface symbols the shim is not in scope to provide.
  If the smoke test omits them, the test is mis-scoped.
- **The `test/smoke/include/__config_site` override matches the
  documented set.** Verbatim from pca006132's recipe (CLAUDE.md /
  plan.md). Diverging from it without explicit rationale is a
  **warning**.
- **Tests are hermetic.** No filesystem dependencies, no network, no
  process-environment reads beyond what CMake sets. Node runner reads
  only its argv.
- **Consumer test is a real `find_package` consumer.** A
  `test/consumer/` that does `add_subdirectory(..)` is testing the
  source tree, not the install tree, and will mask install-rule bugs.
  It must do `find_package(wasm-cxx-shim COMPONENTS …)` against a
  configured `CMAKE_PREFIX_PATH`.
- **Shell scripts driving ctest checks**: any test wrapper that uses
  `set -euo pipefail` AND pipes a tool whose exit code can be 1 on a
  success-condition (e.g., `wasm-objdump -j Import` exits 1 when the
  section is absent — which is our happy path) needs the tool's
  output captured separately:
  `out=$(tool ... 2>&1 || true) && printf '%s\n' "$out" | grep -q ...`
  rather than piping straight. Pipefail propagates the tool's exit 1
  through the pipeline and the script aborts before the condition
  check runs. Both `check-imports.sh` and `run-consumer-test.sh` hit
  this and have the fix in code; new wrappers shouldn't repeat it.
- **Inline shell scripts inside `add_test(COMMAND sh -c "...")`**:
  CMake's quoting eats the inner double-quotes that shell variable
  references need (`case "$out" in *'X'*)`). Use a real shell script
  file invoked via `add_test(COMMAND bash path/to/script.sh ...)`
  instead. We hit this once (Phase 4) and recovered. Future test
  wrappers must not do inline `sh -c`.
- **`ASSERT_*`-in-helper semantics differ from real GoogleTest.**
  Our gtest-shim adapter implements `ASSERT_*` as `WCS_REQUIRE_*`,
  which `return`s from the *helper function*, not from the calling
  TEST. Real GoogleTest sets a `HasFatalFailure()` flag the test
  body would re-check; we don't. When reviewing a new manifold (or
  other consumer) test file, check whether it calls helpers that
  use `ASSERT_*` (in manifold's case: `Identical`, `CheckGL`,
  `CheckStrictly`, `RelatedGL`). Empirically benign so far on
  passing tests; flag as a **note** for awareness — failure-mode
  debugging may surface as multi-failure cascades or unexpected
  state rather than clean aborts. The adapter's preamble
  (`tools/wasm-test-harness/adapters/gtest/gtest.h`) documents the
  trade-off.
- **Test wasms built with `--no-entry`** must `--export=` any
  function whose presence is part of the test's correctness — wasm-ld
  dead-strips functions not reachable from any export. Particularly
  critical for negative-test fixtures (the negative-link gate's
  `unimplemented_calls` would silently vanish without `--export=`,
  passing the link-failure check vacuously). Combined with
  `--error-unresolved-symbols` for strict mode where applicable
  (default wasm-ld behavior is silently lenient about undefined
  symbols).

### 8. Documentation accuracy

- **CLAUDE.md, README.md, docs/context.md, docs/plan.md don't
  contradict each other.** When introducing a change that affects any
  of these (new symbol, new component, scope shift, decision
  reversal), update *all* relevant docs in the same change.
- **No oversold scope.** Per `docs/context.md`: "don't position this
  as a general-purpose freestanding wasm toolchain. It's a shim,
  scoped to demand." If README copy starts implying broader scope,
  flag.
- **Vendored source URLs in component READMEs match what's in the
  repo.** Re-vendoring without updating the README is a **warning**.
- **Symbol lists in component READMEs match the .a contents.** Run
  `llvm-nm` and reconcile against the README. Drift is a **warning**.
- **`docs/plan.md` decision log stays current.** When a Phase finishes
  or its scope changes, update plan.md in the same commit. A stale
  plan that says "TBD" against a finished phase is a **note**.
- **Open questions in `docs/context.md` get resolved or moved.** Once
  the implementation pass closes an open question, update context.md
  ("Allocator choice: dlmalloc, see plan.md" rather than a TBD).
- **Doc-staleness sweep across markdown.** When reviewing release-prep
  PRs, OR any PR that flips a phase/feature/component status, OR any
  PR claiming "X is done now," do a grep across all `*.md`
  (excluding CHANGELOG.md, which legitimately enumerates old
  versions) for these stale-marker patterns and verify each hit is
  either intentional historical narrative or needs an update:
  ```sh
  grep -rn -E "v0\.1[^.]|v0\.2|v0\.3|pre-CI|next step|TBD|TODO|coming|not started|in flight|NEXT" \
      --include='*.md' --exclude=CHANGELOG.md --exclude-dir=build .
  ```
  Common drift patterns:
  - **Duplicated Status sections.** README often has both an intro
    paragraph mentioning status AND a `## Status` H2 section near
    the bottom. When both exist, they diverge across releases.
    Either keep one source of truth (delete the duplicate) or audit
    both on every release. A README with two Status blocks
    referencing different versions is a **warning**.
  - **Hardcoded version numbers in evergreen prose.** "Status: v0.2.0."
    in the README rots on every release. Prefer either no version
    (link to releases/CHANGELOG for per-version detail) or a single
    "current release" pointer that auto-updates. Hardcoded version
    in the README intro is a **note** — fine while the release
    cadence is slow, worth removing if it starts drifting often.
  - **Phase status markers** ("NEXT", "in flight", "not started")
    for phases now DONE — `docs/plan.md` is the usual culprit.
    Stale phase status against landed work is a **warning**.
  - **"TBD" / "TODO" placeholders** that should resolve once the
    referenced thing exists. A `(TBD)` next to a link to a doc that
    is actually fleshed out is a **note**.
  - **Test counts in narrative prose** (e.g., "47/47 tests pass") in
    READMEs that drift when test coverage extends. Either keep
    abstract ("a slice of its tests passes") or commit to updating
    on every test addition. Drift here is a **note**.
- **API renames / removals propagated to docstrings + caller sites.**
  When a CMake helper drops or renames an argument (e.g.,
  `wasm_cxx_shim_add_manifold(CLIPPER2_GIT_TAG ...)` → no longer
  accepted), audit: (a) the helper's own docstring at the top of the
  file, (b) every caller site (`grep -rn 'wasm_cxx_shim_add_manifold'`),
  (c) any README that documents the helper's parameters by name. Same
  for renamed/removed shipped patches: when `0001-foo.patch` becomes
  `0001-bar.patch` (or three patches collapse to one), grep all `*.md`
  for the old patch filenames. A docstring that lists a removed
  argument is a **warning**; a README that names a removed patch is
  also a **warning** since a future agent will look for it and be
  confused.

### 9. Style for hand-written code

- **No comments unless the why is non-obvious.** Comments that
  describe *what* the code does, or reference a current task, are
  **note**-level findings. Per CLAUDE.md.
- **No comments referencing PR numbers, ticket IDs, or
  commit SHAs.** Belongs in commit messages / PR descriptions.
- **`extern "C"` linkage on every C-callable C++ symbol.** A missing
  `extern "C"` on `__cxa_atexit` is a **error**: the symbol gets
  C++-mangled and the linker can't find it.
- **`[[noreturn]]` (or `_Noreturn` in C) on every function that
  doesn't return.** `__cxa_pure_virtual`, `__cxa_throw`,
  `__libcpp_verbose_abort`, `operator new` (when it aborts on null).
- **Header file presence.** Hand-written `.cpp` files don't need
  matching `.h`s; this is a static lib of standalone definitions. A
  `.h` that exists but isn't `#include`-d anywhere is dead.
- **LSP false positives are EXPECTED, not findings.** When the editor
  LSP runs default Apple clang (or any non-wasm-targeted toolchain),
  `__builtin_wasm_memory_grow` and `<features.h>` look "unknown" or
  "file not found." The cross-build is correct; LSP just doesn't have
  our toolchain loaded. Don't surface these as review findings unless
  they also reproduce in a wasm-targeted compile (`cmake --build
  --preset wasm32`). Adding `compile_commands.json` (we do, via
  `CMAKE_EXPORT_COMPILE_COMMANDS=ON`) helps clangd pick up the right
  flags but doesn't help LSPs that lack wasm32 support.

### 10. CI & reproducibility

- **CI matrix covers the supported LLVM range** (18-21 at v0.1)
  on both `ubuntu-latest` and `macos-latest`. Single-OS or
  single-version coverage is a **warning**.
- **CI installs LLVM from a documented source.** apt's
  `apt.llvm.org` repo on Ubuntu, brew on macOS. Any "download a
  random tarball from a URL" step is a **warning**.
- **Reproducible vendoring scripts are runnable.** `scripts/fetch-libm-sources.sh`
  (or whatever) should be runnable cleanly to regenerate the exact
  contents of `libm/src/musl/` etc. Drift between the script's
  intended output and the checked-in files is a **warning**.
- **No flaky tests.** A test that occasionally fails (timing, network
  resolution, allocation-order dependent) is an **error**.
- **CI runs the consumer test as a separate job.** Building with
  `add_subdirectory` inside the same CMake config doesn't validate
  the install + find_package path.

## Output format

Group findings by category. For each finding:

```
**[Category] file:line** severity — Short description.
Suggested fix or explanation.
Why it matters: what breaks and for whom.
```

Severities:

- **error**: link-time or runtime breakage on a supported target;
  ABI mangling mismatch; WASI imports leaking; license violation;
  hard-constraint violation from CLAUDE.md.
- **warning**: likely future breakage (silent drift, untested path,
  doc stale-ness with consequences); style-of-code issue with
  correctness implications (missing `[[noreturn]]`, missing
  `extern "C"`).
- **note**: pure style; doc polish; nice-to-have follow-up.

End with a summary: total findings per category, severity breakdown
(error/warning/note), and recommended priority order for fixes.

If a category has zero findings or doesn't apply to the changed
files, say so explicitly — don't skip silently. The reader needs to
trust that an unmentioned category was actually checked.
