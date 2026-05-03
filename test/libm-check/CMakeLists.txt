# test/libm-check — correctness probes for libm functions.
#
# Asserts that each shipped libm function returns the right value
# for representative inputs. Sanity, not IEEE-precision conformance:
# tolerances are fixed-eps (1e-12 for tight cases, 1e-10 where PI
# inexactness costs a few digits), not 4-ULP.
#
# Catches:
#   * regressions where a libm vendor bump changes a function's
#     behavior (e.g., ABI change, accuracy degradation),
#   * regressions where our compile-flag set (-fno-math-errno,
#     -fno-trapping-math, -ffp-contract=off) breaks something,
#   * link-time regressions where a function silently disappears
#     from the archive (the test wouldn't link).

set(_libm_check_obj  "${CMAKE_CURRENT_BINARY_DIR}/libm-check.cpp.obj")
set(_libm_check_wasm "${CMAKE_CURRENT_BINARY_DIR}/libm-check.wasm")

# Compile as C++ (WCS_TEST static-initializer registration needs C++
# dynamic init). -nostdinc++ keeps libcxx out of the link.
add_custom_command(
    OUTPUT  ${_libm_check_obj}
    COMMAND ${CMAKE_CXX_COMPILER}
        --target=wasm32-unknown-unknown
        -std=c++17 -Os -fno-exceptions -fno-rtti -nostdlib -nostdinc++
        -isystem ${CMAKE_SOURCE_DIR}/tools/wasm-test-harness/include
        -isystem ${CMAKE_SOURCE_DIR}/libm/include
        -isystem ${CMAKE_SOURCE_DIR}/libc/include
        -c ${CMAKE_CURRENT_SOURCE_DIR}/libm_check.cpp
        -o ${_libm_check_obj}
    DEPENDS libm_check.cpp
            wasm-cxx-shim-test-harness
    COMMENT "libm-check: compiling libm_check.cpp"
    VERBATIM
)

add_custom_command(
    OUTPUT  ${_libm_check_wasm}
    COMMAND ${WASM_CXX_SHIM_WASM_LD}
        --no-entry
        --export=wcs_run_tests
        --export=wcs_log_buffer
        --export=wcs_log_size
        ${_libm_check_obj}
        $<TARGET_FILE:wasm-cxx-shim-test-harness>
        $<TARGET_FILE:wasm-cxx-shim-libm>
        $<TARGET_FILE:wasm-cxx-shim-libc>
        -o ${_libm_check_wasm}
    DEPENDS ${_libm_check_obj}
            wasm-cxx-shim-test-harness
            wasm-cxx-shim-libm
            wasm-cxx-shim-libc
    COMMENT "libm-check: linking libm-check.wasm"
    VERBATIM
)

add_custom_target(libm-check ALL DEPENDS ${_libm_check_wasm})

# Re-find wasm-objdump locally rather than relying on the smoke test's
# CMakeLists having populated the cache first. find_program with a
# cache variable is idempotent across calls; this keeps libm-check
# robust against future subdir reordering that would otherwise
# silently drop the imports-check entry.
find_program(WASM_OBJDUMP_EXECUTABLE wasm-objdump)
if(WASM_OBJDUMP_EXECUTABLE)
    add_test(NAME libm_check_imports_check
        COMMAND bash ${CMAKE_SOURCE_DIR}/test/smoke/check-imports.sh
            ${WASM_OBJDUMP_EXECUTABLE}
            ${_libm_check_wasm}
    )
endif()

# Size budget. Current ~17 KB (libm itself is ~10 KB; the test
# harness + per-test thunks add the rest). Budget 24 KB allows for
# adding a few more libm test cases without immediate breakage.
add_test(NAME libm_check_size_budget
    COMMAND bash ${CMAKE_SOURCE_DIR}/test/smoke/check-size.sh
        ${_libm_check_wasm}
        24576
)

find_program(_libm_check_node NAMES node nodejs)
if(_libm_check_node)
    add_test(NAME libm_check_run
        COMMAND ${_libm_check_node}
            ${CMAKE_SOURCE_DIR}/tools/wasm-test-harness/run.mjs
            ${_libm_check_wasm}
    )
endif()
