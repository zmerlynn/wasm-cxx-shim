# tools/wasm-test-harness/test — self-test for the harness itself.
#
# Builds a tiny C-only wasm against the harness library + libc and
# runs it under a custom Node runner that asserts the harness's
# pass/fail counts and log output match what self_test.c is designed
# to produce. The test deliberately includes failing assertions —
# the runner verifies the *count* of failures, not propagates them.
#
# Independent of libcxx by design (mirrors how the harness itself is
# C-only). Builds quickly even on the lightweight CI cells.

set(_self_obj  "${CMAKE_CURRENT_BINARY_DIR}/harness-self-test.cpp.obj")
set(_self_wasm "${CMAKE_CURRENT_BINARY_DIR}/harness-self-test.wasm")

# C++ rather than C: WCS_TEST's static-initializer registration needs
# C++ dynamic initialization. -nostdinc++ means we don't pull libc++
# (the test source includes only <wcs-test.h>).
add_custom_command(
    OUTPUT  ${_self_obj}
    COMMAND ${CMAKE_CXX_COMPILER}
        --target=wasm32-unknown-unknown
        -std=c++17 -Os -fno-exceptions -fno-rtti -nostdlib -nostdinc++
        -isystem ${CMAKE_SOURCE_DIR}/tools/wasm-test-harness/include
        -isystem ${CMAKE_SOURCE_DIR}/libc/include
        -c ${CMAKE_CURRENT_SOURCE_DIR}/self_test.cpp
        -o ${_self_obj}
    DEPENDS self_test.cpp
            wasm-cxx-shim-test-harness    # for the include header
    COMMENT "harness self-test: compiling self_test.cpp"
    VERBATIM
)

add_custom_command(
    OUTPUT  ${_self_wasm}
    COMMAND ${WASM_CXX_SHIM_WASM_LD}
        --no-entry
        --export=wcs_run_tests
        --export=wcs_log_buffer
        --export=wcs_log_size
        ${_self_obj}
        $<TARGET_FILE:wasm-cxx-shim-test-harness>
        $<TARGET_FILE:wasm-cxx-shim-libc>
        -o ${_self_wasm}
    DEPENDS ${_self_obj}
            wasm-cxx-shim-test-harness
            wasm-cxx-shim-libc
    COMMENT "harness self-test: linking harness-self-test.wasm"
    VERBATIM
)

add_custom_target(wasm-cxx-shim-test-harness-self-test ALL
    DEPENDS ${_self_wasm}
)

# Imports check on the self-test wasm. The lightweight build path
# (no manifold) only otherwise asserts no-imports for the smoke wasm;
# the self-test exercises a different link cocktail (harness.a + libc
# only, no libcxx) that's worth its own assertion. Re-find the
# wasm-objdump binary if the smoke test's CMakeLists hasn't already
# populated the cache (subdir order may vary).
find_program(WASM_OBJDUMP_EXECUTABLE wasm-objdump)
if(WASM_OBJDUMP_EXECUTABLE)
    add_test(NAME harness_self_test_imports_check
        COMMAND bash ${CMAKE_SOURCE_DIR}/test/smoke/check-imports.sh
            ${WASM_OBJDUMP_EXECUTABLE}
            ${_self_wasm}
    )
endif()

# Size budget. Current: ~3.5 KB; budget 6 KB. The self-test wasm is
# essentially harness.a + a test source — drift from this baseline
# would mean either the harness grew significantly or the self-test
# source did, both worth surfacing.
add_test(NAME harness_self_test_size_budget
    COMMAND bash ${CMAKE_SOURCE_DIR}/test/smoke/check-size.sh
        ${_self_wasm}
        6144
)

find_program(_self_test_node NAMES node nodejs)
if(_self_test_node)
    add_test(NAME wasm_test_harness_self_test
        COMMAND ${_self_test_node}
            ${CMAKE_CURRENT_SOURCE_DIR}/run.mjs
            ${_self_wasm}
    )
endif()
