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
# Every function in EXPECTED_UNDEFINED below must appear in the link
# errors by name. Checking only for "some undefined symbol" would miss a
# partial regression: someone implementing (say) clock_gettime while
# the other eight stay undefined. Keep the list in step with
# uses_unimplemented.c.

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

# Confirm the failure was for the right reason, symbol by symbol.
# wasm-ld prints "undefined symbol: <name>" for each unresolved external.
EXPECTED_UNDEFINED=(fopen fclose remove rename system clock_gettime open read close)
missing=()
for sym in "${EXPECTED_UNDEFINED[@]}"; do
    if ! grep -qE "undefined (symbol|function):? ${sym}\b" "${WORK}/link.log"; then
        missing+=("${sym}")
    fi
done
if [ "${#missing[@]}" -gt 0 ]; then
    echo "negative-link: FAIL — these are no longer undefined: ${missing[*]}"
    echo "Someone may have added an implementation for an out-of-scope function."
    cat "${WORK}/link.log"
    exit 1
fi
if grep -qE 'undefined symbol|undefined function' "${WORK}/link.log"; then
    echo "negative-link: OK (all ${#EXPECTED_UNDEFINED[@]} out-of-scope functions undefined, as expected)"
    exit 0
fi

echo "negative-link: FAIL — link failed but not with the expected error pattern."
echo "wasm-ld output:"
cat "${WORK}/link.log"
exit 1
