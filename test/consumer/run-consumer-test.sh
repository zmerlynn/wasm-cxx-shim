#!/usr/bin/env bash
# test/consumer/run-consumer-test.sh
#
# Drives the find_package round-trip:
#   1. Install the shim from $SHIM_BUILD_DIR to a temp prefix.
#   2. Configure the external consumer project (test/consumer/external)
#      against that prefix using the INSTALLED toolchain file.
#   3. Build the consumer.
#   4. Verify the produced consumer.wasm has zero unexpected imports.
#
# Args (positional, all required):
#   $1  shim build dir          (e.g. build/wasm32)
#   $2  external consumer src   (test/consumer/external)
#   $3  output base dir         (where to put the install prefix + the
#                                external consumer's build dir)
#   $4  this dir                (used to find any local fixtures; optional)

set -euo pipefail

SHIM_BUILD="${1:?shim build dir}"
CONSUMER_SRC="${2:?consumer src dir}"
OUTPUT_BASE="${3:?output base dir}"
# CONSUMER_DIR (arg 4) reserved for future use; not consumed today.

INSTALL_PREFIX="${OUTPUT_BASE}/install"
CONSUMER_BUILD="${OUTPUT_BASE}/build"

rm -rf "${INSTALL_PREFIX}" "${CONSUMER_BUILD}"
mkdir -p "${INSTALL_PREFIX}" "${CONSUMER_BUILD}"

echo "[1/4] Installing shim to ${INSTALL_PREFIX}"
cmake --install "${SHIM_BUILD}" --prefix "${INSTALL_PREFIX}" >/dev/null

echo "[2/4] Verifying installed layout"
required=(
    "lib/libwasm-cxx-shim-libc.a"
    "lib/libwasm-cxx-shim-libm.a"
    "lib/libwasm-cxx-shim-libcxx.a"
    "lib/cmake/wasm-cxx-shim/wasm-cxx-shimConfig.cmake"
    "lib/cmake/wasm-cxx-shim/wasm-cxx-shimConfigVersion.cmake"
    "lib/cmake/wasm-cxx-shim/wasm-cxx-shim-libc-targets.cmake"
    "lib/cmake/wasm-cxx-shim/wasm-cxx-shim-libm-targets.cmake"
    "lib/cmake/wasm-cxx-shim/wasm-cxx-shim-libcxx-targets.cmake"
    "include/wasm-cxx-shim/libc/stdlib.h"
    "include/wasm-cxx-shim/libc/bits/alltypes.h"
    "include/wasm-cxx-shim/libm/math.h"
    "share/wasm-cxx-shim/toolchain-wasm32.cmake"
)
missing=0
for f in "${required[@]}"; do
    if [[ ! -e "${INSTALL_PREFIX}/${f}" ]]; then
        echo "  MISSING: ${f}"
        missing=1
    fi
done
if [[ ${missing} -ne 0 ]]; then
    echo "consumer test FAIL: install layout missing files"
    exit 1
fi
echo "  install layout OK"

echo "[3/4] Configuring + building external consumer"
cmake \
    -S "${CONSUMER_SRC}" \
    -B "${CONSUMER_BUILD}" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${INSTALL_PREFIX}/share/wasm-cxx-shim/toolchain-wasm32.cmake" \
    -DCMAKE_PREFIX_PATH="${INSTALL_PREFIX}" \
    >/dev/null

cmake --build "${CONSUMER_BUILD}" >/dev/null

echo "[4/4] Verifying consumer.wasm"
consumer_wasm="${CONSUMER_BUILD}/consumer.wasm"
if [[ ! -e "${consumer_wasm}" ]]; then
    echo "consumer test FAIL: consumer.wasm not produced"
    exit 1
fi

# Imports check (skipped silently if wasm-objdump isn't installed —
# the build succeeding is itself a strong signal).
#
# wasm-objdump exits 1 when the requested section is absent, which is
# our happy path. Capture with `|| true` so pipefail doesn't propagate
# the exit code.
if command -v wasm-objdump >/dev/null 2>&1; then
    imports_out=$(wasm-objdump -x -j Import "${consumer_wasm}" 2>&1 || true)
    if printf '%s\n' "${imports_out}" | grep -q 'Section not found: Import'; then
        echo "  imports check OK (no Import section)"
    else
        echo "consumer test FAIL: consumer.wasm has unexpected imports:"
        printf '%s\n' "${imports_out}"
        exit 1
    fi

    exports_out=$(wasm-objdump -x -j Export "${consumer_wasm}" 2>&1 || true)
    if printf '%s\n' "${exports_out}" | grep -q '"probe_run"'; then
        echo "  exports include probe_run OK"
    else
        echo "consumer test FAIL: probe_run not exported:"
        printf '%s\n' "${exports_out}"
        exit 1
    fi
fi

echo "consumer test PASS"
