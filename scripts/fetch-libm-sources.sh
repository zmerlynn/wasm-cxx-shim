#!/usr/bin/env bash
# fetch-libm-sources.sh — reproducibly vendor musl libm into libm/src/.
#
# Pulls from WebAssembly/wasi-libc's musl tree (NOT upstream musl). The
# wasi-libc tree carries the wasm-specific tweaks already applied, most
# notably alternate-rounding paths gated out of __rem_pio2.c via
# __wasilibc_unmodified_upstream.
#
# Run from anywhere; resolves project root via git-rev-parse.
#
# Override the upstream commit by setting WASI_LIBC_REF=<sha-or-tag>
# in the environment. Defaults to `main`.

set -euo pipefail

REPO_ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
LIBM_DIR="${REPO_ROOT}/libm"
WASI_LIBC_REF="${WASI_LIBC_REF:-main}"
BASE="https://raw.githubusercontent.com/WebAssembly/wasi-libc/${WASI_LIBC_REF}/libc-top-half/musl"

# 42 .c files in src/math/, double-precision plus workhorses
MATH_C_FILES=(
    __cos.c __sin.c __tan.c
    __rem_pio2.c __rem_pio2_large.c
    __math_divzero.c __math_invalid.c __math_oflow.c
    __math_uflow.c __math_xflow.c
    acos.c asin.c atan.c atan2.c
    ceil.c copysign.c cos.c sin.c tan.c
    exp.c exp_data.c
    fabs.c floor.c fma.c
    fmax.c fmin.c
    hypot.c ilogb.c ldexp.c
    log.c log_data.c log10.c
    log2.c log2_data.c
    pow.c pow_data.c
    remquo.c round.c scalbn.c
    sqrt.c sqrt_data.c trunc.c
)

# 5 _data.h files, co-located with their .c counterparts
MATH_H_FILES=(
    exp_data.h log_data.h log2_data.h pow_data.h sqrt_data.h
)

# Internal headers from src/internal/, src/include/, arch/wasm32/
INTERNAL_FILES=(
    "src/internal/libm.h"
    "src/internal/atomic.h"
    "src/include/features.h"
    "arch/wasm32/atomic_arch.h"
    "arch/wasm32/fp_arch.h"
)

# Public math.h chain, served from libm/include/.
# NOT fetched: features.h (hand-rolled to match libc/include/features.h
# verbatim — they must be identical for component-independence; the
# wasi-libc/musl features.h has wasi-bottom-half-specific bits).
# NOT fetched: bits/alltypes.h (hand-rolled).
PUBLIC_FILES=(
    "include/math.h"
)

mkdir -p "${LIBM_DIR}/src/musl" "${LIBM_DIR}/src/internal" "${LIBM_DIR}/include/bits"

echo "Fetching from ${BASE}..."

for f in "${MATH_C_FILES[@]}" "${MATH_H_FILES[@]}"; do
    curl -fsSL "${BASE}/src/math/${f}" -o "${LIBM_DIR}/src/musl/${f}"
done

for path in "${INTERNAL_FILES[@]}"; do
    name="$(basename "${path}")"
    curl -fsSL "${BASE}/${path}" -o "${LIBM_DIR}/src/internal/${name}"
done

for path in "${PUBLIC_FILES[@]}"; do
    name="$(basename "${path}")"
    curl -fsSL "${BASE}/${path}" -o "${LIBM_DIR}/include/${name}"
done

echo "Vendored $((${#MATH_C_FILES[@]} + ${#MATH_H_FILES[@]})) files into libm/src/musl/"
echo "Vendored ${#INTERNAL_FILES[@]} headers into libm/src/internal/"
echo "Vendored ${#PUBLIC_FILES[@]} headers into libm/include/"
echo ""
echo "Hand-written (NOT fetched):"
echo "  libm/include/features.h        (must match libc/include/features.h exactly)"
echo "  libm/include/bits/alltypes.h   (must match libc/include/bits/alltypes.h exactly)"
echo "  libm/src/internal/endian.h     (5-line shim, wasm is little-endian)"
