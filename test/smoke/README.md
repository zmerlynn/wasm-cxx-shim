# Smoke test

End-to-end exerciser for the shim. Building it for `wasm32-unknown-unknown`,
linking against the three shim components, and getting both:

- a `.wasm` with **zero imports** (`smoke_imports_check`), and
- a successful `run()` returning the expected integer
  (`smoke_run`)

is the v0.1 acceptance signal that the shim is doing its job.

## What this exercises

`smoke.cpp` exercises the canonical STL path through real libc++
headers:

- `std::vector<int>` push_back / iteration → operator new[] (vector
  growth), memcpy/memmove (relocation), operator delete[]
- `std::unordered_map<int,int>` insert / iterate → `__next_prime`
  (libcxx/src/stubs.cpp), bucket allocation through operator new
- Virtual class with virtual dtor through a base pointer → libcxx
  vtable + `std::exception` key-function path
- `std::sqrt` + `std::sin` from `<cmath>` → libm

Resulting wasm: **~14 KB, zero imports**, `run()` returns 109.

## How libc++'s headers are made to compile

Without configuration, libc++'s `<vector>` / `<unordered_map>` /
`<cmath>` headers transitively pull in `<stdio.h>`, `<stdlib.h>`,
`<wchar.h>`, `<time.h>`, `<bits/types/*.h>` — none of which
`wasm32-unknown-unknown` clang ships out of the box, and most of which
imply runtime symbols (`abort`, `clock_gettime`, etc.) we deliberately
don't ship either.

This smoke makes the chain work via two mechanisms:

1. **`libc/include/`** ships musl-style headers (`<stdio.h>`,
   `<stdlib.h>`, `<wchar.h>`, `<time.h>`, etc.) that declare types
   and functions but mostly DO NOT implement them. Calling
   `fopen`/`time`/etc. produces a link-time error, which is correct —
   those operations are out of scope for the shim.

2. **`test/smoke/include/__config_site`** is a libc++ override (loaded
   first via `-isystem`) that disables threads, filesystem, locale,
   wide-chars, monotonic clock, and the time-zone DB at the libc++
   level. Without it, libc++ headers drag in symbols our shim
   doesn't and shouldn't ship.

   Adapted from [pca006132's manifold-on-wasm
   config](https://github.com/elalish/manifold/discussions/1046#discussioncomment-11302257),
   updated for LLVM 20+'s numeric-value config macros (LLVM 19 used
   defined/undefined; LLVM 20 switched to numeric 0/1).

   In particular: `_LIBCPP_HAS_MUSL_LIBC 1` tells libc++ to use the
   musl-style `__NEED_mbstate_t` / `<bits/alltypes.h>` path for
   resolving `mbstate_t` (rather than glibc's `<bits/types/*.h>` or
   the wide-char fallback), which matches the headers shipped by
   `libc/include/`.

3. **`libcxx/src/stubs.cpp`'s `__next_prime`** stubs out the one
   `unordered_map` symbol that would otherwise require compiling
   libc++'s `hash.cpp`. Three further libc++ source TUs from
   pca006132's recipe (`bind.cpp`, `memory.cpp`, `new_helpers.cpp`)
   are NOT stubbed; the smoke doesn't reach for them. Real consumers
   using `std::bind` placeholders, `std::shared_ptr`, or `std::nothrow`
   will surface new missing symbols and need to compile those TUs
   themselves. See [`libcxx/README.md`](../../libcxx/README.md) for
   the per-symbol details.

## How it's wired

`CMakeLists.txt` here drives:

1. Compile `smoke.cpp` to a wasm `.o` via `clang++
   --target=wasm32-unknown-unknown -nostdinc++ -nostdlib`. Include order
   on the `-isystem` chain: `test/smoke/include` (our `__config_site`)
   → libc++ headers → `libm/include` → `libc/include`.
2. Link the `.o` against the three shim archives via `wasm-ld
   --no-entry --export=run`.
3. `wasm-objdump -x -j Import` (via `check-imports.sh`) asserts the
   resulting `.wasm` has no Import section.
4. `node run.mjs <wasm>` instantiates and calls `run()`, asserting the
   return value.

## Expected return value

```
sum(1..5)           = 15
sum(1²..5²)         = 1 + 4 + 9 + 16 + 25 = 55
Impl(7).compute()   = 7
sqrt(1024.0)        = 32
sin(0.0) * 1000     = 0
─────────────────────────
total               = 109
```

## When this test fails

- **`smoke_imports_check` fails with non-empty Import section**: the
  shim is missing a symbol the consumer references. Run
  `wasm-objdump -x -j Import build/wasm32/test/smoke/smoke.wasm` to
  see what's missing, and add a stub to `libcxx/src/stubs.cpp` (or to
  `libc/`/`libm/` if it's a C-runtime/math symbol).
- **`smoke_run` fails with mismatched return value**: a runtime bug
  in one of the components. Likely culprits: dlmalloc cocktail tuning
  (`MALLOC_ALIGNMENT`, page size), libm function correctness, or
  operator new/delete returning unaligned memory.

## Files

- `smoke.cpp` — the probe (vector + unordered_map + virtual dtor + libm)
- `run.mjs` — Node ESM runner; instantiates wasm, calls `run()`,
  asserts return value
- `check-imports.sh` — wasm-objdump-based imports-empty check
- `include/__config_site` — pca006132-derived libc++ site config
  (LLVM 20+ format), wired in via `-isystem`
- `include/__assertion_handler` — libc++ hardening assertion handler
  override (routes hardening assertions to `__builtin_trap`)
