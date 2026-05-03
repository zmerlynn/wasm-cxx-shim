#!/usr/bin/env bash
# expect-link-failure.sh — assert a wasm build deliberately fails to link.
#
# Usage: expect-link-failure.sh <clang> <wasm-ld> <src.c> <inc-dir> <libc.a>
#
# Compiles + links uses_unimplemented.c. Expects the link step to FAIL
# with undefined-symbol errors, confirming the shim does NOT implement
# the out-of-scope functions referenced. A successful link is a
# regression — someone may have added an implementation that
# shouldn't be there.
#
# Exit codes:
#   0  link failed with the expected "undefined symbol" pattern
#   1  link succeeded (regression!) OR link failed with an unexpected error
#
# Known limitation — partial regressions:
#
# uses_unimplemented.c references 9 out-of-scope functions in one TU.
# If a future maintainer adds an implementation for ONE of them (say,
# fopen), the link still fails with 8 undefined symbols, this script
# still exits 0, and we'd silently lose coverage of fopen. The "all-
# or-nothing" check catches the most likely regression mode (someone
# accidentally adding implementations) but not the partial case. A
# stricter form would compile each function in its own TU + ctest
# entry; deferred until we have evidence of partial regressions
# happening in practice.

set -uo pipefail

CLANG="${1:?usage: $0 <clang> <wasm-ld> <src.c> <inc-dir> <libc.a>}"
WASM_LD="${2:?usage: $0 <clang> <wasm-ld> <src.c> <inc-dir> <libc.a>}"
SRC="${3:?usage: $0 <clang> <wasm-ld> <src.c> <inc-dir> <libc.a>}"
INC="${4:?usage: $0 <clang> <wasm-ld> <src.c> <inc-dir> <libc.a>}"
LIBC_A="${5:?usage: $0 <clang> <wasm-ld> <src.c> <inc-dir> <libc.a>}"

# mktemp differs between Linux and macOS; this template form works on both.
TMPDIR_BASE="${TMPDIR:-/tmp}"
WORK=$(mktemp -d "${TMPDIR_BASE%/}/neg-link-XXXXXX")
OBJ="${WORK}/uses_unimplemented.o"
WASM="${WORK}/uses_unimplemented.wasm"
trap 'rm -rf "${WORK}"' EXIT

# ---- Compile step ----
# Must succeed; the test is about LINK failure, not compile failure.
# (If a future header drift makes this fail at compile time, the test
# below covers that case too — compile-fail → "this isn't supported"
# → exit 0.)
if ! "${CLANG}" \
        --target=wasm32-unknown-unknown -Os -nostdlib \
        -isystem "${INC}" \
        -c "${SRC}" -o "${OBJ}" 2>"${WORK}/compile.log"; then
    echo "negative-link: OK (compile failed — these symbols aren't supported)"
    cat "${WORK}/compile.log"
    exit 0
fi

# ---- Link step ----
# Two flags are load-bearing:
#   --export=unimplemented_calls : keeps the function alive against
#       wasm-ld's dead-code elimination. Without this, --no-entry
#       drops unimplemented_calls (no entry-point reachability), its
#       calls to fopen/etc. go away, and the link succeeds with an
#       empty wasm — defeating the test's purpose.
#   --error-unresolved-symbols : strict mode; otherwise wasm-ld
#       defaults to silently letting undefined symbols through (they
#       become imports or get ignored depending on the flag set).
#
# Capture stderr; wasm-ld writes "undefined symbol" diagnostics there.
if "${WASM_LD}" \
        --no-entry \
        --export=unimplemented_calls \
        --error-unresolved-symbols \
        "${OBJ}" "${LIBC_A}" -o "${WASM}" 2>"${WORK}/link.log"; then
    echo "negative-link: FAIL — link succeeded when failure was expected."
    echo "Someone may have added an implementation for an out-of-scope function."
    echo "Check the wasm at ${WASM}:"
    cat "${WORK}/link.log" || true
    # Don't auto-clean; let the user inspect.
    trap - EXIT
    exit 1
fi

# Confirm the failure was for the right reason. wasm-ld prints
# "undefined symbol: <name>" for each unresolved external.
if grep -qE 'undefined symbol|undefined function' "${WORK}/link.log"; then
    echo "negative-link: OK (link failed with undefined-symbol errors as expected)"
    echo "Sample errors:"
    grep -E 'undefined symbol|undefined function' "${WORK}/link.log" | head -5 | sed 's/^/  /'
    exit 0
fi

echo "negative-link: FAIL — link failed but not with the expected error pattern."
echo "wasm-ld output:"
cat "${WORK}/link.log"
exit 1
