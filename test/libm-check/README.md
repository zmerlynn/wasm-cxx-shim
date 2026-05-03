# libm correctness probes

Sanity-level correctness tests for every libm function the shim
ships. Not IEEE-precision conformance — these check that
`sqrt(0)=0`, `sin(π/2)=1`, `pow(2,10)=1024`, etc. against fixed-eps
tolerances.

The point isn't bit-exactness across LLVM versions; it's catching
the failure modes the no-imports check doesn't:

- A libm vendor bump (e.g., a wasi-libc rev) silently changes a
  function's behavior.
- A compile-flag change (`-fno-math-errno`, `-fno-trapping-math`,
  `-ffp-contract=off`) introduces a regression in something we
  didn't intend to touch.
- A function silently disappears from the archive (the test
  wouldn't link, surfacing the loss).

Coverage: 23 tests (one or more per function), exercising the 27
public libm symbols — sqrt, fabs, sin, cos, tan, asin, acos, atan,
atan2, exp, log, log10, log2, pow, hypot, fma, fmax, fmin, ceil,
floor, trunc, round, copysign, ldexp, scalbn, ilogb, remquo.

## Tolerances

- **1e-12** default: tight enough to catch real regressions, loose
  enough that minor accuracy variations across LLVM versions don't
  trip.
- **1e-10** for π-involved cases: π is approximated to ~1.2e-16
  below true value as a `double`, so `sin(π)` is approximately
  `1.22e-16` rather than 0. The 1e-10 margin still flags a regression
  in sin's behavior near π, just not the inherent representation
  loss.
- **`& 0x7`** for `remquo`'s quotient: C11 §7.12.10.3 only requires
  the low ≥3 bits of `*quo` to match the integral quotient. musl
  returns the full quotient; a different libm could legally fold.
  The mask keeps the test vendor-portable.

## How it's wired

```
libm_check.cpp  (compiled as C++ for WCS_TEST static-init)
        │
        ↓
   <wcs-test.h>     <math.h>
        │             │
        ↓             ↓
   wcs-test-harness.a   wasm-cxx-shim-libm.a    wasm-cxx-shim-libc.a
                            │
                            ↓
                    libm-check.wasm  →  node run.mjs
```

No libc++ involvement: `-nostdinc++` keeps libcxx out of the link;
the test source uses only `<math.h>` + `<wcs-test.h>`. Same shape as
the harness self-test, just exercising libm instead of harness
primitives.

## Files

- `libm_check.cpp` — the test source (C++, WCS_TEST registry).
- `CMakeLists.txt` — compile + link + ctest entries
  (`libm_check_imports_check`, `libm_check_size_budget`,
  `libm_check_run`).

## Expected output

```
wasm-test-harness: starting...
[   1/  23] LibM.sqrt ... ok
[   2/  23] LibM.fabs ... ok
...
[  23/  23] LibM.remquo ... ok
wasm-test-harness: 23 passed, 0 failed, 23 total
```
