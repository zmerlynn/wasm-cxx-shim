# libm — math library subset for wasm32-unknown-unknown.
#
# Independently buildable. 42 .c files vendored from musl via wasi-libc,
# plus internal helper headers and a public <math.h> chain.
#
# See README.md for symbol list, source provenance, and swap instructions.

# 42 .c files — the 15 named functions (acos asin atan2 cos sin pow fma
# fmax fmin hypot ilogb log10 log2 remquo round) + workhorses + helpers.
# Glob-free for reproducibility: a stray file in src/musl/ won't silently
# get pulled in.
set(WASM_CXX_SHIM_LIBM_SOURCES
    src/musl/__cos.c
    src/musl/__sin.c
    src/musl/__tan.c
    src/musl/__rem_pio2.c
    src/musl/__rem_pio2_large.c
    src/musl/__math_divzero.c
    src/musl/__math_invalid.c
    src/musl/__math_oflow.c
    src/musl/__math_uflow.c
    src/musl/__math_xflow.c
    src/musl/acos.c
    src/musl/asin.c
    src/musl/atan.c
    src/musl/atan2.c
    src/musl/ceil.c
    src/musl/copysign.c
    src/musl/cos.c
    src/musl/sin.c
    src/musl/tan.c
    src/musl/exp.c
    src/musl/exp_data.c
    src/musl/fabs.c
    src/musl/floor.c
    src/musl/fma.c
    src/musl/fmax.c
    src/musl/fmin.c
    src/musl/hypot.c
    src/musl/ilogb.c
    src/musl/ldexp.c
    src/musl/log.c
    src/musl/log_data.c
    src/musl/log10.c
    src/musl/log2.c
    src/musl/log2_data.c
    src/musl/pow.c
    src/musl/pow_data.c
    src/musl/remquo.c
    src/musl/round.c
    src/musl/scalbn.c
    src/musl/sqrt.c
    src/musl/sqrt_data.c
    src/musl/trunc.c
)

add_library(wasm-cxx-shim-libm STATIC ${WASM_CXX_SHIM_LIBM_SOURCES})
add_library(wasm-cxx-shim::libm ALIAS wasm-cxx-shim-libm)
set_target_properties(wasm-cxx-shim-libm PROPERTIES EXPORT_NAME libm)

target_include_directories(wasm-cxx-shim-libm
    # Public headers consumers see: our musl-derived <math.h> chain.
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
# `BEFORE` is load-bearing: musl's `src/include/features.h` overrides the
# public `include/features.h` to add `hidden`/`weak`/`weak_alias` macros
# that musl libm sources rely on. Keep the override path EARLIER on the
# include search than the public path.
target_include_directories(wasm-cxx-shim-libm BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/musl
)

# ---- install ----
# Consumers `find_package(wasm-cxx-shim COMPONENTS libm)` and link
# wasm-cxx-shim::libm. Per-component include subdir keeps libm
# self-sufficient; both libm and libc ship a `bits/alltypes.h` (same
# content) so each is independently usable.
target_include_directories(wasm-cxx-shim-libm INTERFACE
    $<INSTALL_INTERFACE:include/wasm-cxx-shim/libm>
)

# GNUInstallDirs included at top-level; CMAKE_INSTALL_* vars inherited.

install(TARGETS wasm-cxx-shim-libm
    EXPORT wasm-cxx-shim-libm-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wasm-cxx-shim/libm
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT wasm-cxx-shim-libm-targets
    NAMESPACE   wasm-cxx-shim::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wasm-cxx-shim
)

target_compile_options(wasm-cxx-shim-libm PRIVATE
    -Os
    # Stability: never let codegen fold mul+add into a fused-multiply-add
    # automatically. Results are then bit-identical regardless of consumer
    # compile flags or wasm runtime FMA support. We're a numerical-stack
    # shim — leaning toward determinism over a small speed win.
    -ffp-contract=off
    -fno-math-errno     # match musl behavior; we don't have errno anyway
    -fno-trapping-math  # match wasi-libc; redundant on wasm but harmless

    # Warning suppressions for vendored musl code (mirrors wasi-libc).
    -Wno-bitwise-op-parentheses
    -Wno-dangling-else
    -Wno-ignored-attributes
    -Wno-ignored-pragmas
    -Wno-logical-op-parentheses
    -Wno-macro-redefined  # clang's <float.h> defines NAN/INFINITY ahead of musl's <math.h>
    -Wno-missing-braces
    -Wno-parentheses
    -Wno-shift-op-parentheses
    -Wno-sign-compare
    -Wno-string-plus-int
    -Wno-unknown-pragmas
    -Wno-unused-but-set-variable
    -Wno-unused-function
    -Wno-unused-parameter
    -Wno-unused-variable
)
