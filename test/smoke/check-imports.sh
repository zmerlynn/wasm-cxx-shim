#!/usr/bin/env bash
# check-imports.sh — assert a wasm has no Import section.
#
# Usage: check-imports.sh <wasm-objdump-path> <wasm-file>
#
# Exits 0 if the wasm has no Import section (or empty one), nonzero
# otherwise. Used by the smoke_imports_check ctest entry.

set -uo pipefail

WASM_OBJDUMP="${1:?usage: $0 <wasm-objdump> <wasm-file>}"
WASM_FILE="${2:?usage: $0 <wasm-objdump> <wasm-file>}"

# wasm-objdump exits 1 when the requested section isn't found. That's
# our happy path, so capture stdout+stderr and don't propagate the
# exit code.
out=$("${WASM_OBJDUMP}" -x -j Import "${WASM_FILE}" 2>&1 || true)

# When there's no Import section, wasm-objdump prints
#     Section not found: Import
# (to stderr, captured via 2>&1 above) followed by the empty
# section-details preamble. That's our happy path.
if printf '%s\n' "${out}" | grep -q 'Section not found: Import'; then
    echo "imports-check: OK (no Import section)"
    exit 0
fi

# Otherwise, an Import section exists. Print the contents and fail.
echo "imports-check: FAIL — wasm has unexpected imports:"
printf '%s\n' "${out}"
exit 1
