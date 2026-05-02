# LICENSES

License texts for third-party code vendored into this repo. The repo
itself is MIT (see top-level `LICENSE`); these files cover the
upstream code we redistribute.

| File | Covers | Compatible with MIT |
|---|---|---|
| `LICENSE-musl` | musl libm + mem* sources under `libc/src/musl/`, `libm/src/musl/`, `libm/src/internal/`, plus our vendored `<stdio.h>`/`<stdlib.h>` | yes (MIT) |
| `LICENSE-dlmalloc` | `libc/src/dlmalloc/malloc.c` (Doug Lea malloc v2.8.6) | yes (CC0) |

In addition, several `.c` files under `libm/src/musl/` carry their own
per-file copyright headers (FreeBSD/Sun Microsystems, Arm Limited).
Those headers are preserved verbatim within each file. Their license
terms are MIT-compatible.

The wasi-libc-derived wrapper code (`libc/src/dlmalloc/dlmalloc.c`,
`libc/src/dlmalloc/sbrk.c`) is dual-licensed by the upstream WebAssembly
project under Apache-2.0-with-LLVM-exception OR MIT. We redistribute
under MIT (matching this repo's top-level license).

## Sources

- musl libc: https://www.musl-libc.org/ (mirror: https://github.com/ifduyue/musl)
- wasi-libc: https://github.com/WebAssembly/wasi-libc
- dlmalloc: https://gee.cs.oswego.edu/pub/misc/malloc.c
