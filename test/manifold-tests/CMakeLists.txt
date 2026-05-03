# test/manifold-tests — manifold's GoogleTest-based test suite, running
# on wasm32-unknown-unknown via the wasm-test-harness.
#
# First proof-point: a single test file (test/boolean_test.cpp from
# manifold v3.4.1) plus manifold's own test/test_main.cpp (which
# defines `Options options;` and the helpers like ExpectMeshes /
# RelatedGL / CheckGL that consumer test files call). Both are
# compiled against our gtest-shim adapter, linked against manifold +
# Clipper2 + the shim, and run via Node. ASSERTs and EXPECTs in the
# test source produce log lines via the harness's log buffer; the
# runner reports counts; exit status reflects pass/fail.
#
# Manifold's test_main.cpp normally provides a real `int main()` plus
# filesystem-based fixture loading. Both are wrapped in
# `#ifndef MANIFOLD_NO_FILESYSTEM` by patches/0003-... and we compile
# with -DMANIFOLD_NO_FILESYSTEM=1, leaving just the helpers + Options.
# The wasm export is `wcs_run_tests` from the harness; the host
# invokes that directly instead of main().
#
# Follow-on: incrementally add more manifold test files. Each addition
# is just an entry in `_manifold_test_files` below; the harness scales
# with N tests at no additional infrastructure cost.

# Reuse the libcxx headers detection from the smoke test setup.
get_filename_component(_clang_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(_llvm_root "${_clang_dir}" DIRECTORY)
set(_libcxx_headers "${_llvm_root}/include/c++/v1")

# manifold's test sources live in the FetchContent'd manifold-src tree;
# manifold-link's CMakeLists ensures FetchContent_MakeAvailable(manifold)
# has already run by the time we get here.
set(_manifold_src "${manifold_SOURCE_DIR}")

# The manifold test files we compile + run in this build. Add more
# here as we extend coverage. test_main.cpp must be in the list — it
# provides Options, helpers, and (under MANIFOLD_NO_FILESYSTEM) stubs
# for the file-I/O symbols other tests reference.
set(_manifold_test_files
    ${_manifold_src}/test/test_main.cpp
    ${_manifold_src}/test/boolean_test.cpp
    ${_manifold_src}/test/sdf_test.cpp
    ${_manifold_src}/test/cross_section_test.cpp
)

# Each test source compiles with:
#   * gtest-shim BEFORE real GoogleTest's include path so
#     `#include "gtest/gtest.h"` resolves to our adapter.
#   * harness <wcs-test.h> available.
#   * manifold's own test/ dir on the include path so test.h resolves.
#   * libc++ + shim public headers + __config_site override.
set(_test_objs)

set(_common_compile_flags
    --target=wasm32-unknown-unknown
    -std=c++17 -Os -fno-exceptions -fno-rtti -nostdlib -nostdinc++
    -DMANIFOLD_NO_IOSTREAM=1
    -DMANIFOLD_NO_FILESYSTEM=1       # disables main()+filesystem fixture I/O in test_main.cpp
    -DMANIFOLD_PAR=-1                # serial backend; src/utils.h requires this
    -DCLIPPER2_MAX_DECIMAL_PRECISION=8  # match manifold's compile flag for clipper2
    -isystem ${CMAKE_SOURCE_DIR}/test/smoke/include            # __config_site (shared with smoke + manifold-link)
    # Cross-test-dir reuse: pulls the <mutex> stub out of
    # test/manifold-link/include/ rather than duplicating it here. NOT a
    # CMake target dependency — just sharing a header file. If
    # test/manifold-link/ is ever moved or its include layout changes,
    # update this path. Keep these two test dirs together in any
    # restructuring audit.
    -isystem ${CMAKE_SOURCE_DIR}/test/manifold-link/include    # <mutex> stub
    -isystem ${CMAKE_SOURCE_DIR}/tools/wasm-test-harness/adapters/gtest  # gtest-shim
    -isystem ${CMAKE_SOURCE_DIR}/tools/wasm-test-harness/include          # <wcs-test.h>
    -isystem ${_libcxx_headers}
    -isystem ${CMAKE_SOURCE_DIR}/libm/include
    -isystem ${CMAKE_SOURCE_DIR}/libc/include
    -I ${_manifold_src}/include
    -I ${_manifold_src}/test                                   # for test.h
    -I ${_manifold_src}/src                                    # for src/utils.h
    -I ${manifold_BINARY_DIR}/include                          # for generated headers
    -I ${clipper2_SOURCE_DIR}/CPP/Clipper2Lib/include
)

# Compile each manifold test source.
foreach(_src IN LISTS _manifold_test_files)
    get_filename_component(_src_basename "${_src}" NAME_WE)
    set(_obj "${CMAKE_CURRENT_BINARY_DIR}/${_src_basename}.cpp.obj")
    add_custom_command(
        OUTPUT  ${_obj}
        COMMAND ${CMAKE_CXX_COMPILER}
            ${_common_compile_flags}
            -c ${_src}
            -o ${_obj}
        DEPENDS ${_src}
                wasm-cxx-shim-test-harness
        COMMENT "manifold-tests: compiling ${_src_basename}.cpp"
        VERBATIM
    )
    list(APPEND _test_objs ${_obj})
endforeach()

# Link the wasm: test objects + harness + libcxx-extras + manifold +
# Clipper2 + shim libs. Export wcs_run_tests / wcs_log_buffer /
# wcs_log_size for the Node runner.
set(_tests_wasm "${CMAKE_CURRENT_BINARY_DIR}/manifold-tests.wasm")
set(_libcxx_extras_obj_for_tests
    "${CMAKE_BINARY_DIR}/test/manifold-link/libcxx-extras.cpp.obj"
)

add_custom_command(
    OUTPUT  ${_tests_wasm}
    COMMAND ${WASM_CXX_SHIM_WASM_LD}
        --no-entry
        --export=wcs_run_tests
        --export=wcs_log_buffer
        --export=wcs_log_size
        ${_test_objs}
        ${_libcxx_extras_obj_for_tests}
        $<TARGET_FILE:wasm-cxx-shim-test-harness>
        $<TARGET_FILE:manifoldc>
        $<TARGET_FILE:manifold>
        $<TARGET_FILE:Clipper2>
        $<TARGET_FILE:wasm-cxx-shim-libcxx>
        $<TARGET_FILE:wasm-cxx-shim-libc>
        $<TARGET_FILE:wasm-cxx-shim-libm>
        -o ${_tests_wasm}
    DEPENDS ${_test_objs}
            ${_libcxx_extras_obj_for_tests}
            wasm-cxx-shim-test-harness
            manifoldc manifold Clipper2
            wasm-cxx-shim-libc wasm-cxx-shim-libm wasm-cxx-shim-libcxx
    COMMENT "manifold-tests: linking manifold-tests.wasm"
    VERBATIM
)

add_custom_target(manifold-tests ALL DEPENDS ${_tests_wasm})

# Imports check: assert manifold-tests.wasm has no Import section,
# mirroring the smoke + manifold-link probe checks. The headline
# property of this project (zero unexpected imports) belongs on every
# wasm we ship, not just the lightweight ones — a future libcxx-extras
# extension could pull in a stray symbol that gets surfaced as an
# import, and we'd want CI to catch it here.
if(WASM_OBJDUMP_EXECUTABLE)
    add_test(NAME manifold_tests_imports_check
        COMMAND bash ${CMAKE_SOURCE_DIR}/test/smoke/check-imports.sh
            ${WASM_OBJDUMP_EXECUTABLE}
            ${_tests_wasm}
    )
endif()

# Size budget. Current: ~915 KB (boolean+SDF+cross_section); budget
# 1.1 MB allows for adding 1-2 more test files of similar shape
# without immediate breakage. A bigger jump (e.g., adding manifold_test.cpp)
# would legitimately blow this budget — bump it then with a why-line.
add_test(NAME manifold_tests_size_budget
    COMMAND bash ${CMAKE_SOURCE_DIR}/test/smoke/check-size.sh
        ${_tests_wasm}
        1153434
)

# Test runner: `node tools/wasm-test-harness/run.mjs <wasm>`.
find_program(_manifold_tests_node NAMES node nodejs)
if(_manifold_tests_node)
    add_test(NAME manifold_tests_run
        COMMAND ${_manifold_tests_node}
            ${CMAKE_SOURCE_DIR}/tools/wasm-test-harness/run.mjs
            ${_tests_wasm}
    )
endif()
