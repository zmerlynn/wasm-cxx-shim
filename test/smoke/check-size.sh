#!/usr/bin/env bash
# check-size.sh — assert a wasm artifact is under a byte budget.
#
# Usage: check-size.sh <wasm-file> <max-bytes>
#
# Exits 0 if the file size is < max-bytes, 1 otherwise. Used by
# *_size_budget ctest entries to provide early warning of accidental
# code-bloat regressions. Budgets are hand-tuned to current size +
# ~15-20% to allow normal growth without immediate breakage; raise
# the budget in the corresponding CMakeLists.txt when growth is
# intentional and document the why in the bump-commit's message.

set -euo pipefail

WASM_FILE="${1:?usage: $0 <wasm-file> <max-bytes>}"
MAX_BYTES="${2:?usage: $0 <wasm-file> <max-bytes>}"

if [ ! -f "${WASM_FILE}" ]; then
    echo "size-budget: FAIL — file does not exist: ${WASM_FILE}"
    echo "Did the build target produce it? Re-run cmake --build ..."
    exit 1
fi

# Portable byte count (avoids stat(1)'s incompatible flags between Linux + macOS).
actual=$(wc -c < "${WASM_FILE}" | tr -d ' ')

basename=$(basename "${WASM_FILE}")
if [ "${actual}" -lt "${MAX_BYTES}" ]; then
    echo "size-budget: OK (${basename}: ${actual} < ${MAX_BYTES})"
    exit 0
fi

echo "size-budget: FAIL — ${basename} is ${actual} bytes (budget: ${MAX_BYTES})"
echo "If this growth is intentional, raise the budget in the corresponding CMakeLists.txt"
echo "and document the rationale in the bump-commit message."
exit 1
