# libc — minimal C standard library subset

Provides the C runtime symbols required by `wasm32-unknown-unknown`
consumers that use `std::vector`, `std::string`, and other STL types
that need an allocator and a memory-copy primitive.

## Symbols provided

Allocator (dlmalloc):
- `malloc`, `free`, `calloc`, `realloc`, `aligned_alloc`

Memory + string ops (musl):
- `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`

Internal-only:
- `sbrk` — synthetic, wraps `__builtin_wasm_memory_grow`. Despite the
  name, this is **not a syscall**; it produces no WASI imports.

## Headers provided

A small set of musl-style headers consumers (and libc++) need to
compile against this shim:

| Header | Origin | Notes |
|---|---|---|
| `<string.h>` | hand-written | declares mem* family + `strlen` |
| `<endian.h>` | hand-written | wasm-cxx-shim's endian shim |
| `<features.h>` | musl-derived | source-feature flags + `__restrict`, `__inline`, `_Noreturn` |
| `<bits/alltypes.h>` | hand-written | musl-style typedef provider keyed off `__NEED_*` macros (size_t, FILE, mbstate_t, …) |
| `<stdlib.h>` | upstream musl verbatim | `abort`, `ldiv_t/lldiv_t`, allocator API, etc. |
| `<stdio.h>` | upstream musl verbatim | `FILE`/`fpos_t`, `remove`/`rename`/`fopen`/...; **declarations only** — no implementations |
| `<wchar.h>` | hand-written | minimal: only the type plumbing (`mbstate_t`, `wint_t`); no `wmemcpy`/`wcscpy`/etc. |
| `<alloca.h>` | hand-written | `#define alloca __builtin_alloca` |
| `<time.h>` | hand-written | type names only (`time_t`, `clock_t`, `tm`, `timespec`); no `time()`/`clock_gettime()` |

Most stdio/stdlib **functions** declared above are unimplemented — calling
`fopen`, `time`, `wcscpy` etc. produces a link-time error. That's
intentional: the headers exist so libc++'s STL chain compiles; the
runtime symbols are explicitly *not* in scope.

## Symbols NOT provided

By demand only. Not in scope for v0.1:

- `posix_memalign`, `malloc_usable_size` — add when a consumer needs them.
- `errno` global, `errno.h` macros — dlmalloc is configured with
  `LACKS_ERRNO_H`; no consumer has asked for them yet.
- POSIX/syscall wrappers (`open`, `read`, `clock_gettime`, etc.) —
  permanently out of scope. If your code needs these, you want WASI SDK
  instead of this shim.
- Threading primitives — out of scope.
- `string.h` beyond the `mem*` family (the `str*` family, locale-aware
  variants, etc.) — add by demand.

## Source provenance

| File | Upstream | License | Notes |
|---|---|---|---|
| `src/dlmalloc/malloc.c` | [WebAssembly/wasi-libc `dlmalloc/src/malloc.c`](https://github.com/WebAssembly/wasi-libc/blob/main/dlmalloc/src/malloc.c) (Doug Lea malloc v2.8.6 verbatim) | CC0 / public domain | unmodified |
| `src/dlmalloc/dlmalloc.c` | derived from [wasi-libc `dlmalloc/src/dlmalloc.c`](https://github.com/WebAssembly/wasi-libc/blob/main/dlmalloc/src/dlmalloc.c) | MIT (also Apache-2.0) | trimmed: drops musl alias machinery, `<malloc.h>`/`<errno.h>` deps, `posix_memalign`/`malloc_usable_size`. Hardcodes wasm page size; inlines `ENOMEM`/`EINVAL` |
| `src/dlmalloc/sbrk.c` | derived from [wasi-libc `libc-bottom-half/sources/sbrk.c`](https://github.com/WebAssembly/wasi-libc/blob/main/libc-bottom-half/sources/sbrk.c) | MIT | trimmed: `<errno.h>`/`<unistd.h>`/`<stdlib.h>` deps, `errno` set on OOM |
| `src/musl/memcpy.c`,<br>`src/musl/memmove.c`,<br>`src/musl/memset.c`,<br>`src/musl/memcmp.c`,<br>`src/musl/strlen.c` | [wasi-libc/libc-top-half/musl/src/string/](https://github.com/WebAssembly/wasi-libc/tree/main/libc-top-half/musl/src/string) (musl) | MIT | unmodified (musl uses a project-wide LICENSE; no per-file headers to preserve) |

The wasi-libc-derived files inherit wasm-specific tweaks already applied
by the WASI SDK team — most importantly the bulk-memory threshold gating
in `mem*.c`.

## dlmalloc cocktail

The `#define`s in `dlmalloc.c` configure malloc.c for freestanding wasm32.
Concrete values:

```c
HAVE_MMAP=0           MORECORE_CANNOT_TRIM=1
ABORT=__builtin_unreachable()
LACKS_TIME_H=1        LACKS_ERRNO_H=1       LACKS_STDLIB_H=1
LACKS_STRING_H=1      LACKS_UNISTD_H=1      LACKS_SCHED_H=1
LACKS_SYS_PARAM_H=1   LACKS_SYS_TYPES_H=1   LACKS_FCNTL_H=1
NO_MALLINFO=1         NO_MALLOC_STATS=1
MALLOC_ALIGNMENT=16   USE_DL_PREFIX=1       DLMALLOC_EXPORT=static inline
malloc_getpagesize=65536
ENOMEM=12             EINVAL=22
MALLOC_FAILURE_ACTION=/* no-op */
```

Plus `BULK_MEMORY_THRESHOLD=200` (passed to musl `mem*.c` from
`libc/CMakeLists.txt`).

Don't change a single value here without updating `CLAUDE.md`'s mirror
of this table; the cocktail is load-bearing for wasm correctness.

## What this depends on

- A wasm-ld-provided `__heap_base` symbol — the linker generates it
  pointing at the top of static data. dlmalloc/sbrk grow upward from
  there. wasm-ld provides this for any wasm output by default.
- Clang's freestanding `<stddef.h>` and `<stdint.h>` headers (for
  `size_t`, `intptr_t`, `SIZE_MAX`).
- Bulk-memory wasm feature (default-on for clang ≥ 16 on wasm32). If a
  consumer disables it (`-mno-bulk-memory`), the musl `mem*` functions
  silently fall back to their inline byte/word loops; no link-time
  effect.

## Component independence

This component does **not** link `wasm-cxx-shim::libm` or
`wasm-cxx-shim::libcxx`. A downstream consumer can use this libc
without the others — say, a numeric Rust crate that only needs
`malloc` + `memcpy`. The CMake target is `wasm-cxx-shim::libc`.

## Replacing this component

If you have a better allocator or memory routines (e.g., a custom
arena allocator, or a future wasm-native allocator that beats
dlmalloc's binary size), drop this component
(`-DWASM_CXX_SHIM_BUILD_LIBC=OFF`) and provide the symbols yourself.
The libm and libcxx components are unaffected.

`libcxx`'s `operator new` calls `malloc()` directly; if you replace
this libc, your replacement must define a malloc that takes
`size_t` and returns `void*`. Same shape for `free`, `aligned_alloc`.
