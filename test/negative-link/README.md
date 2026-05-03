# Negative-link gate

A **negative test**: success means the build deliberately fails.

The shim ships musl-derived `<stdio.h>` and `<stdlib.h>` headers
that *declare* functions like `fopen`, `fclose`, `remove`, `rename`,
`system`, but provide no implementations. The README's promise to
consumers is that calling those functions produces a link-time
error, not a working wasm with a syscall they didn't expect.

This test makes that promise machine-verified instead of merely
asserted-in-prose. If a future maintainer accidentally adds an
implementation for one of these (e.g., copy-pasted from another
project), the link succeeds, this test exits 1, and CI catches the
regression before a downstream consumer is the one to notice.

## What it covers

`uses_unimplemented.c` references nine out-of-scope functions:

| Function       | Source of declaration                  |
|----------------|----------------------------------------|
| `fopen`        | `<stdio.h>` (upstream musl, declared)  |
| `fclose`       | `<stdio.h>`                            |
| `remove`       | `<stdio.h>`                            |
| `rename`       | `<stdio.h>`                            |
| `system`       | `<stdlib.h>` (upstream musl, declared) |
| `clock_gettime`| extern declaration in this file        |
| `open`         | extern declaration                     |
| `read`         | extern declaration                     |
| `close`        | extern declaration                     |

The first five are declared by headers we ship; compile succeeds,
link fails. The last four are reached via plain `extern` declarations
because the shim's headers don't declare them at all (and we want
the compile step to succeed, with the LINK being the thing that
fails).

## Two wasm-ld flags are load-bearing

The script applies both:

- `--export=unimplemented_calls` — keeps the function alive against
  wasm-ld's dead-code elimination. Without it, `--no-entry` drops
  `unimplemented_calls` (no entry-point reachability), the calls to
  `fopen`/etc. disappear, and the link succeeds with an empty wasm.
- `--error-unresolved-symbols` — strict mode. Default behavior is
  silently lenient (undefined symbols get treated as imports or
  ignored, depending on flags).

Both are documented at the call site in `expect-link-failure.sh`.

## Known limitation: partial regressions

If a future maintainer adds an implementation for ONE of the nine
(say, `fopen`), the link still fails with eight undefined symbols,
the script still exits 0, and we'd silently lose coverage of fopen.
The all-or-nothing check catches the most likely regression mode
(someone adds implementations) but not the partial case. Stricter
form: one ctest entry per function. Deferred until we have evidence
of partial regressions happening in practice.

## Files

- `uses_unimplemented.c` — the deliberately-unbuildable source.
- `expect-link-failure.sh` — runs compile + link, asserts link
  fails with `undefined symbol` errors. Exits 0 on expected
  failure, 1 on either successful link (regression!) or unexpected
  error.
- `CMakeLists.txt` — single ctest entry
  `negative_link_unimplemented`.

## Expected output

```
negative-link: OK (link failed with undefined-symbol errors as expected)
Sample errors:
  wasm-ld: error: ...: undefined symbol: fopen
  wasm-ld: error: ...: undefined symbol: fclose
  wasm-ld: error: ...: undefined symbol: remove
  wasm-ld: error: ...: undefined symbol: rename
  wasm-ld: error: ...: undefined symbol: system
```
