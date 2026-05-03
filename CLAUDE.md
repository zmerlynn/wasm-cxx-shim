# CLAUDE.md — agent context for wasm-cxx-shim

## What this project is

A minimal, CMake-installable C and C++ runtime shim for the
`wasm32-unknown-unknown` Rust/Clang target. The target ships with no
libc, no libc++, and no libc++abi, which makes it impossible to link
non-trivial C++ code against it out of the box. WASI SDK and Emscripten
both fill the gap but bind you to their respective platforms (WASI
imports, JS shims). This shim provides the smaller, narrower thing you
need to compile a self-contained C++ kernel and link it against
`wasm-bindgen`-style wasm without dragging in either ecosystem.

The first concrete consumer is
[manifold-csg](https://github.com/zmerlynn/manifold-csg) (Rust bindings
to the manifold3d CSG kernel). Scope grows by demand — a symbol gets
added when a real consumer reports the linker complaining about it,
not proactively for completeness.

Background and motivation: [`docs/context.md`](docs/context.md).
Implementation roadmap: [`docs/plan.md`](docs/plan.md). Read both
before making meaningful changes.

## Repo layout

```
.
├── CMakeLists.txt           top-level aggregator
├── cmake/                   toolchain file, find_package config template
├── docs/
│   ├── context.md           why the project exists, prior art, decisions
│   └── plan.md              implementation roadmap, decision log, risk register
├── libc/                    allocator + mem* (component 1)
│   └── src/
│       ├── dlmalloc/        Doug Lea malloc + wasi-libc wrapper, sbrk shim
│       └── musl/            memcpy/memmove/memset/memcmp from musl
├── libm/                    math (component 2)
│   ├── include/             vendored <math.h> chain (musl-derived)
│   └── src/
│       ├── musl/            42 .c + 5 .h from musl, via wasi-libc tree
│       └── internal/        libm.h + arch headers + endian shim
├── libcxx/                  C++ ABI/runtime stubs (component 3)
│   └── src/                 4 hand-written .cpp files (~150 lines total)
├── test/
│   ├── smoke/               vector + unordered_map + virtual dtor smoke test
│   └── consumer/            external find_package consumer test
├── scripts/                 reproducible vendoring helpers
└── .github/workflows/       CI
```

## Hard constraints (do not violate)

These come straight from the design and are tested for in the review
skill. Don't break them without an architectural conversation first.

- **No inter-component CMake dependencies.** `libcxx`'s `operator new`
  needs `malloc`, but the dependency is documented in `libcxx/README`,
  not expressed as `target_link_libraries(libcxx libc)`. Each component
  must be independently buildable and swappable. A consumer satisfying
  `malloc` from elsewhere should be able to skip our `libc` entirely.
- **No WASI imports.** The output `.wasm` must have zero `__wasi_*`
  imports. The smoke test runs `wasm-objdump -j Import` and asserts
  the imports list is empty (or only contains `__heap_base`, which is
  linker-provided and resolves at link time, not import time). Anything
  that pulls in `<unistd.h>` for syscalls, `<time.h>` for `clock_gettime`,
  `<fcntl.h>`, `<dirent.h>`, etc. is forbidden.
- **No threading.** No pthreads, no `_LIBCPP_HAS_NO_THREADS=0` paths,
  no thread-local storage, no atomics that imply a runtime. The
  consumer compiles with libc++'s `_LIBCPP_HAS_NO_THREADS` defined.
- **No exception runtime.** Stub `__cxa_throw` as `__builtin_trap()`.
  Consumers compile `-fno-exceptions`. Implicit STL throws
  (`std::bad_alloc`, etc.) become unrecoverable crashes — that's
  acceptable.
- **MIT-license-compatible vendoring only.** Preserve the original
  copyright header verbatim in every vendored source file. Acceptable
  upstream licenses: MIT (musl), CC0/public domain (dlmalloc),
  Apache-2.0-with-LLVM-exception (libc++/libc++abi reference patterns),
  BSD-2 / BSD-3 (compatible). Reject anything copyleft.

## Toolchain requirements

Apple clang has no `wasm32` target. The CMake toolchain file
(`cmake/toolchain-wasm32.cmake`) auto-detects a wasm-capable clang in
this order:

1. `${WASM_CXX_SHIM_CLANG}` if set in the environment.
2. A homebrewed `llvm@N` install with a sibling `lld` install
   (`/opt/homebrew/opt/llvm@N/bin/clang` + `/opt/homebrew/opt/lld@N/bin/wasm-ld`).
3. Emscripten's bundled LLVM at
   `/opt/homebrew/Cellar/emscripten/<ver>/libexec/llvm/bin/`. This is
   complete (clang + wasm-ld + llvm-ar + llvm-ranlib + lld) and is
   typically present on dev machines that already do related wasm
   work. CI installs upstream LLVM directly.

Required tools at runtime: `clang`, `clang++`, `wasm-ld`, `llvm-ar`,
`llvm-ranlib`. Plus `wabt` for `wasm-objdump` / `wasm-nm` (used by
test assertions). `node v20+` for the smoke test runner.

## Build & test

```sh
# Configure once
cmake --preset wasm32

# Build all components
cmake --build --preset wasm32

# Run the smoke test (requires Node)
ctest --preset wasm32 --output-on-failure
```

Common iteration loops:

```sh
# Reconfigure after CMake changes
cmake --preset wasm32

# Iterate on a single component
cmake --build --preset wasm32 --target wasm-cxx-shim-libc

# Inspect the produced archive
llvm-ar t build/wasm32/libc/libwasm-cxx-shim-libc.a
llvm-nm   build/wasm32/libc/libwasm-cxx-shim-libc.a | grep ' [BDRT] '

# Inspect a linked wasm (the smoke test, once Phase 4 lands)
wasm-objdump -x -j Import build/wasm32/test/smoke/smoke.wasm  # `-x` is required
wasm-objdump -x -j Export build/wasm32/test/smoke/smoke.wasm
llvm-nm -u   build/wasm32/test/smoke/smoke.wasm               # undefined: should be empty
```

## Vendoring rules

We pull source from upstream projects (musl, dlmalloc, wasi-libc, libc++).
Rules:

1. **Preserve the original license header verbatim** at the top of
   every vendored file that has one. Don't reformat it. For files
   without a per-file header (musl ships with project-wide LICENSE),
   ensure the project's LICENSE text is shipped in `LICENSES/` at the
   repo root.
2. **Every vendored or cribbed file links to its source.** Either:
   - The upstream URL is in the file's top comment (preferred for
     hand-modified or newly-introduced vendored files), OR
   - The file is listed in the surrounding component's README source
     provenance table with a URL.

   Both forms are acceptable; one of them must exist for *every* file
   that didn't originate as wasm-cxx-shim hand-written code. This is
   load-bearing: a future maintainer reading a file with no provenance
   may assume it's hand-written and modify it, breaking re-vendoring.
   The review skill (Cat 4) checks this.
3. **Document the source URL** for every vendored file in a single
   place per component: `libc/README.md`, `libm/README.md`. Pin the
   source to a specific commit SHA on upstream (not a branch HEAD)
   when possible. The reproducible-vendoring scripts under `scripts/`
   should regenerate the exact same bytes.
4. **Modifications are tracked.** If we have to patch a vendored file,
   wrap the change with `// wasm-cxx-shim: ...` comments so it's
   greppable. Document the *why* in the surrounding component's
   README. Resist the urge to clean up unrelated style — diff churn
   makes future re-vendoring painful.
5. **Take wasi-libc's musl tree for libm, not upstream musl directly.**
   wasi-libc's `libc-top-half/musl/src/math/` is
   musl-with-wasm-tweaks-already-applied. Of our 42 closure files,
   only `__rem_pio2.c` differs from upstream musl; the other 41 are
   byte-identical. Inheriting wasi-libc's tree means we inherit their
   ongoing maintenance.
6. **For libc public headers** (`stdio.h`, `stdlib.h`, etc.), upstream
   musl is the canonical source — wasi-libc's version pulls in
   wasi-bottom-half-specific headers we don't have (`<__functions_malloc.h>`,
   `<__header_stdlib.h>`).

## Coding style for hand-written code

- **Default: no comments.** Only add a comment when the *why* is
  non-obvious — a hidden constraint, a subtle invariant, a workaround
  for a specific upstream-LLVM bug, behavior that would surprise a
  reader.
- **Don't reference current task or transient context** in comments.
  No "added for the smoke test", no "see PR #12". That belongs in the
  commit message or PR description.
- **C code:** C11. Match musl style for memory/string ops (don't
  reformat dlmalloc — it's vendored). Avoid GNU extensions in our
  hand-written shims unless `__builtin_*` is the only way (e.g.,
  `__builtin_wasm_memory_grow`, `__builtin_trap`).
- **C++ code:** C++17. Don't `#include` libc++ headers in stub TUs
  that define ABI symbols — define minimal local declarations and
  rely on the Itanium mangling. This is the deliberate strategy that
  insulates us from libc++ header drift across LLVM versions.
- **No backwards-compat shims.** This is v0.x; we can break consumers
  freely. Don't add deprecated aliases or compat headers.
- **No new top-level files** unless asked. Keep the layout compact.

## ABI mangling reference (libcxx component)

These symbols are load-bearing and easy to miss-name. Reference values
verified empirically via `c++filt` and via reading
`llvm-project/libcxx/include/...`:

| Symbol | Itanium mangling | Namespace | Notes |
|---|---|---|---|
| `std::__1::__libcpp_verbose_abort(char const*, ...)` | `_ZNSt3__122__libcpp_verbose_abortEPKcz` | versioned (`std::__1::`) | `noexcept` does NOT affect mangling; our stub omits it for portability across LLVM 16-21 |
| `std::exception::~exception()` (D2/D1/D0) | `_ZNSt9exceptionD2Ev` / `D1Ev` / `D0Ev` | **un**versioned (`std::`) | NOT `St3__19exception` — class lives outside the versioned namespace |
| `vtable for std::exception` | `_ZTVSt9exception` | unversioned | emitted automatically by the TU defining `~exception()` out of line |
| `typeinfo for std::exception` | `_ZTISt9exception` | unversioned | same |

**The unversioned-vs-versioned namespace distinction matters.** If you
get `std::exception::~exception()` mangled as `_ZNSt3__19exception...`
the link will fail and the error will be cryptic.

## dlmalloc cocktail (libc component)

The full `#define` cocktail in `libc/src/dlmalloc/dlmalloc.c`. **The
authoritative copy is the file itself**; this section mirrors it for
quick reference. If you change the file, update this list, and update
`libc/README.md`'s cocktail table in the same commit.

```c
#define HAVE_MMAP            0
#define MORECORE_CANNOT_TRIM 1
#define ABORT                __builtin_unreachable()
#define LACKS_TIME_H         1
#define LACKS_ERRNO_H        1
#define LACKS_STDLIB_H       1
#define LACKS_STRING_H       1
#define LACKS_UNISTD_H       1
#define LACKS_SCHED_H        1
#define LACKS_SYS_PARAM_H    1
#define LACKS_SYS_TYPES_H    1
#define LACKS_FCNTL_H        1
#define NO_MALLINFO          1
#define NO_MALLOC_STATS      1
#define MALLOC_ALIGNMENT     16
#define USE_DL_PREFIX        1
#define DLMALLOC_EXPORT      static inline
#define malloc_getpagesize   65536
#define ENOMEM               12  /* matches musl */
#define EINVAL               22  /* matches musl */
#define MALLOC_FAILURE_ACTION    /* no-op: errno is unavailable */
#include "malloc.c"  /* upstream Doug Lea v2.8.6 verbatim */
/* then re-export the public names */
void* malloc(size_t s)               { return dlmalloc(s); }
void  free(void* p)                  { dlfree(p); }
void* calloc(size_t n, size_t s)     { return dlcalloc(n, s); }
void* realloc(void* p, size_t s)     { return dlrealloc(p, s); }
void* aligned_alloc(size_t a, size_t s) { return dlmemalign(a, s); }
```

Plus `BULK_MEMORY_THRESHOLD=200` passed via `-D` from
`libc/CMakeLists.txt` to the musl mem* TUs.

**Don't add `posix_memalign` or `malloc_usable_size`** unless a
consumer reports needing them; keeping the public surface minimal keeps
dead-strip effective.

## Things to ALWAYS verify before claiming a phase done

- `wasm-objdump -j Import build/test/smoke/smoke.wasm` shows an empty
  import section (or only `__heap_base`).
- `llvm-nm -u build/test/smoke/smoke.wasm` shows no undefined symbols.
- `node test/smoke/run.mjs build/test/smoke/smoke.wasm` exits 0 and
  prints the expected return value (`47` for the canonical smoke).
- All license headers in vendored files are intact (`grep -L 'Copyright' libc/src/dlmalloc/*.c` should be empty).
- The CI matrix passes on at least one OS × LLVM-version combo before
  declaring a fix landed.

## Sharp edges (hard-won lessons)

These bit me during the v0.1 implementation and would absolutely bite a
future agent the same way. Each is also captured in a code comment at
the call site, but listing them here saves a future-me from re-deriving
them.

### Headers and the `__NEED_*` / `__DEFINED_*` interlock

- **`libc/include/bits/alltypes.h` is intentionally NOT include-guarded.**
  musl's design has each public header set its own subset of `__NEED_*`
  macros and `#include <bits/alltypes.h>` — relying on the per-typedef
  `__DEFINED_*` interlock to make repeat inclusion safe. An outer
  include guard short-circuits the second inclusion (with a different
  `__NEED_*` set), and you get "unknown type name 'FILE'" or similar at
  the consumption site.
- **`libc/include/bits/alltypes.h` and `libm/include/bits/alltypes.h`
  must be byte-identical.** Same for `features.h`. Both components ship
  their own copy (component-independence is intentional), but whichever
  `-isystem` path comes first wins. If they drift, the lookup order
  silently affects behavior. The `fetch-libm-sources.sh` script does
  NOT pull these — they're hand-rolled in both places.

### libc++ knobs

- **`_LIBCPP_HAS_MUSL_LIBC=1`** in the smoke test's `__config_site` is
  load-bearing. It tells libc++ to take the musl-style
  `__NEED_mbstate_t` / `<bits/alltypes.h>` path for resolving
  `mbstate_t`, instead of glibc's `<bits/types/mbstate_t.h>` or the
  wide-char fallback. Without it, libc++'s `<__mbstate_t.h>`
  errors out: "We don't know how to get the definition of mbstate_t
  without `<wchar.h>` on your platform."
- **LLVM 20 changed the `__config_site` macro convention** from
  defined-or-not (`_LIBCPP_HAS_NO_THREADS`, etc.) to numeric
  (`_LIBCPP_HAS_THREADS 1`/`0`). pca006132's recipe was for LLVM 19;
  our `__config_site` is updated for LLVM 20+. If we ever need to
  support 19-and-earlier, that's a new `__config_site` variant.

### Compile flags

- **`-fno-builtin` in libc's compile flags is non-optional.** Without
  it, clang sees our hand-written memcpy / memset / etc. and helpfully
  substitutes a memcpy call back — which is exactly what we're trying
  to *implement*. Infinite recursion at runtime if it slips through.
- **`-DBULK_MEMORY_THRESHOLD=200`** is required for the musl mem*
  sources because clang ≥ 16 enables `__wasm_bulk_memory__` by default
  for wasm32. Without the define, those files fail to compile (the
  bulk-memory branch references the threshold).
- **`MALLOC_FAILURE_ACTION` defaults to `errno = ENOMEM;`** in
  dlmalloc. With `LACKS_ERRNO_H` set you don't have errno, so this
  expansion produces an "undeclared identifier 'errno'" error at every
  failure-action site. Override to a no-op: `#define
  MALLOC_FAILURE_ACTION /* no-op */` *before* `#include "malloc.c"`.

### CMake

- **`install(EXPORT NAMESPACE wasm-cxx-shim::)` prepends to the FULL
  target name.** A library named `wasm-cxx-shim-libc` exports as
  `wasm-cxx-shim::wasm-cxx-shim-libc`, NOT `wasm-cxx-shim::libc`. To
  get the latter, set `set_target_properties(... PROPERTIES
  EXPORT_NAME libc)` per target.
- **`include(GNUInstallDirs)` must come BEFORE `add_subdirectory()`**
  if the subdirs use `CMAKE_INSTALL_LIBDIR`/`INCLUDEDIR`/etc. (We do.)
  Putting it after produces install rules with empty paths and
  cryptic "file INSTALL cannot make directory '/wasm-cxx-shim/libc'"
  errors at install time.
- **`PROJECT VERSION 0.0.0`** plus `write_basic_package_version_file
  COMPATIBILITY SameMinorVersion` produces a corner case where
  `find_package(wasm-cxx-shim)` with no version requested fails with
  "The version found is not compatible with the version requested."
  Use a non-zero minor (`0.1.0` etc.) and `COMPATIBILITY AnyNewerVersion`
  while we're sub-1.0.
- **`add_test(COMMAND ${CMAKE_COMMAND} -E env ... sh -c "...")` mangles
  inline quotes.** CMake's quoting eats the inner double-quotes around
  shell variable references, breaking case-statement patterns and
  everything else with embedded quotes. Use a real shell script file
  invoked via `add_test(COMMAND bash path/to/script.sh ...)` instead.
- **`enable_testing()` must precede any `add_subdirectory()` that
  calls `add_test()`.** Late-binding silently drops the test entries
  — the `add_test` call in the subdir is a no-op when testing wasn't
  enabled in (or before) its enclosing scope. The build still
  succeeds and the wasm still builds; ctest just lists fewer entries
  than the CMake source implies. We hit this when adding the harness
  self-test in PR #4 and fixed it by hoisting `enable_testing()`
  above all test-subdir adds in the top-level CMakeLists.

### wasm-ld and test wasms

- **`--no-entry` dead-strips functions not reachable from any export.**
  When building a test wasm with `--no-entry` (we do — there's no
  conventional `_start` for our wasms), wasm-ld removes any function
  that isn't reachable through `--export=`-listed symbols. Tests that
  need a function preserved for inspection (e.g., the negative-link
  gate's `unimplemented_calls`, which exists *to* surface its own
  undefined references) MUST `--export=` it explicitly. Without the
  export, the function vanishes, the undefined symbols it references
  vanish with it, and you get a "successful" link of an empty wasm —
  which silently passes whatever check you wrote.
- **wasm-ld is silently lenient about undefined symbols by default.**
  Default behavior treats undefined references as imports or just
  ignores them (depending on the flag set). For test wasms that need
  to FAIL on unresolved symbols (negative-link gate, link-correctness
  asserts), pass `--error-unresolved-symbols`. For test wasms that
  need to deliberately allow undefined (rare), `--allow-undefined` is
  the explicit opt-in. The default is the surprise.
- **`WCS_TEST` static-init registration requires C++ compilation.**
  The macro expands to a static initializer that calls
  `wcs_register_test()`. Pure-C file-scope initializers must be
  constant expressions per the C standard; a function call isn't.
  Test sources using the harness must be `.cpp` even if the body is
  C-flavored (no STL, no exceptions). Compiling as `.c` produces a
  cryptic "initializer element is not a compile-time constant" error
  at every WCS_TEST. The harness header documents this; the rule
  mirrors here so it doesn't get lost.

### Shell scripting

- **`set -euo pipefail` + a tool that exits 1 on a success-condition
  is a footgun.** `wasm-objdump -j Import file.wasm` exits 1 when the
  Import section is absent — which is our happy path. With pipefail,
  the pipeline status is 1 and the script aborts before our condition
  check runs. Capture the output separately with `out=$(... 2>&1 ||
  true)` and grep that, rather than piping straight to grep.

### Operating discipline

- **Phase scoping**: if a phase as planned can't fit (e.g., Phase 4's
  STL smoke turned out to need a libc-headers tree we hadn't built),
  *split* the phase (4 → 4 + 4b) and update plan.md in the same diff
  as the implementation. Silently rescoping a phase produces
  documentation that says one thing and code that does another, and
  the gap compounds.

## Things to NEVER do

- Don't add an `add_subdirectory(libc)` from `libcxx/CMakeLists.txt`
  (or any other inter-component CMake dependency). See "Hard
  constraints" above.
- Don't pull in WASI sysroot. We're targeting `wasm32-unknown-unknown`,
  not `wasm32-wasi`. Mixing them produces wasm with WASI imports that
  wasm-bindgen consumers can't load.
- Don't copy libc++ source files (`bind.cpp`, `hash.cpp`, etc.) into
  this repo. Those belong in the consumer's LLVM tree; this shim
  provides only the layer LLVM doesn't ship.
- Don't ship a real `__cxa_throw`. The wasm exception runtime is
  meaningfully more work and is out of scope for v0.x.
- Don't grow scope by demand-of-future-features. Add a symbol when a
  real consumer reports it missing; not before.
- Don't set `MALLOC_ALIGNMENT` below 16 — `std::align_val_t(16)` is the
  most common alignment request, and dropping it forces dlmalloc into
  a slow path.
- Don't push to `main` without CI green. Use a feature branch + PR.

## Operating notes for agents

- **Plan before edit for any cross-file change.** Read `docs/plan.md`
  first; it sequences the work for a reason. If a phase needs to slip
  or change, update plan.md in the same commit as the implementation
  diff so the plan stays accurate.
- **Use `/review` (the project review skill) before claiming work
  done.** It's tuned for this project's risk areas (wasm linkability,
  ABI mangling, vendored-source provenance).
- **When stuck on a missing symbol**: `wasm-ld --warn-unresolved-symbols`
  + `wasm-nm -u` is the iteration loop. Add the symbol to
  `libcxx/src/stubs.cpp` as a placeholder, get the link clean, then
  decide whether the placeholder should be promoted to a real stub.
- **Don't auto-format vendored code.** clang-format will happily
  reformat 6300 lines of dlmalloc; resist.

## Commit history policy: one session, one commit

Each Claude Code "session" — the back-and-forth thread between the user
returning to the project and the user wrapping up — collapses to a
single commit at session end. The public history is linear, and each
commit on `main` is roughly self-contained: passes the full test
suite, has a coherent message, and represents a meaningful unit of
progress.

**Working pattern within a session:**

- Make as many intermediate commits as feels natural while the work is
  in flight (per-phase, per-fix, etc.). They're scratch work for
  legibility during the session.
- Before pushing or before the session wraps, squash all of the
  session's commits down to one. Use `git rebase -i <last-pre-session>`
  or `git reset --soft <last-pre-session> && git commit`.
- Compose the final message as a coherent narrative — not a literal
  concatenation of the intermediate commit messages. Lead with what the
  session accomplished; group fixes/refactors/doc updates by theme;
  call out non-obvious decisions and their rationale.

**Why:** intermediate commits during exploratory implementation are
noisy. A future reader of `git log` doesn't care that I split Phase 4
into 4+4b in flight — they care that the project moved from "scaffold"
to "v0.1 STL-smoke green" in one coherent step. Bisecting a regression
is also cleaner across fewer, better-tested commits.

**When NOT to squash a session:**

- A session that genuinely produces multiple distinct deliverables
  (e.g., "fix bug X, then add unrelated feature Y") can stay as
  multiple commits — the principle is "one commit per logical unit",
  and a session sometimes contains more than one.
- Any commit that's already been pushed and pulled by someone else.
  Once history is shared, don't rewrite it.

**Squash mechanics:**

The cleanest path is `git reset --soft <last-pre-session-sha>` then a
single `git commit`. That preserves the working tree exactly as it is,
discards the intermediate commit objects, and lets you compose the
final message from scratch rather than editing pick/squash markers.
Force-push (`git push --force-with-lease`) only after confirming the
branch hasn't been pulled by anyone else.

## All changes to `main` go through PRs

After the v0.1 initial push, **never push directly to `main`.** Every
change — including doc tweaks, CI fixes, anything — lands via a PR.
Workflow:

1. Branch off `main`: `git checkout -b <descriptive-branch-name>`.
2. Make whatever changes the work requires. Commit as you go;
   intra-branch commits are exploratory and will be squashed.
3. Before pushing the branch, squash the intra-branch commits into
   one (per the "one session, one commit" rule above).
4. `git push -u origin <branch>`, then `gh pr create --fill` (or
   compose title/body explicitly).
5. Wait for CI green. Then **stop and hand back to the user.**
6. After the user confirms the merge happened, `git checkout main &&
   git pull && git remote prune origin` to sync local main.

**Why**: history on `main` stays linear, every change has a CI run
attached, and the human stays in the loop on what lands.

**The merge guardrail (load-bearing)**: Claude opens the PR and
reports CI status, but **does NOT run `gh pr merge`** — even after
self-review, even on its own work, even when CI is green. The user
either merges via the GitHub UI / their own `gh pr merge`, or
explicitly says "merge it." Without an explicit instruction, treat a
green CI as the END of the loop, not a green light to merge.

This is enforced by branch protection on `main` (required status
checks + required linear history + no force pushes), but the
human-in-the-loop gate for the merge action itself is a guardrail at
the agent level rather than the GitHub level — partly so the policy
is visible in this file rather than buried in repo settings, and
partly so even an admin user (who can bypass branch protection)
doesn't accidentally let an agent merge.

**Exception**: the very first commit (the v0.1 initial push) goes
direct, because there's no `main` to PR against yet. After that, PRs
all the way down.
