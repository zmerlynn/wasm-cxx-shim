# test/manifold-link — link upstream manifold against the shim.
#
# Phase 7-precursor: prove that elalish/manifold builds + links against
# our shim with zero unexpected wasm imports. Using FetchContent (not
# a submodule) so the integration mirrors how downstream consumers
# would actually pull manifold + the shim into their own builds.
#
# Opt-in via -DWASM_CXX_SHIM_BUILD_MANIFOLD_LINK=ON. Default OFF
# because the FetchContent clone of manifold + Clipper2 + dependencies
# is multi-MB and minutes to build — not something we want to inflict
# on every developer's edit-build loop.
#
# Dogfoods the wasm_cxx_shim_add_manifold() helper (cmake/WasmCxxShim
# Manifold.cmake): the high-change-rate parts of the cocktail
# (FetchContent pins, patches, manifold's CMake options, base
# compile flags) are now centralized there. This file handles only
# the shim-side specifics (`__config_site`, `<mutex>` stub, libcxx
# headers location).

# Load the helper. (When this CMakeLists is consumed via the parent
# project's add_subdirectory, find_package isn't in play; include it
# directly from the source tree.)
include(${CMAKE_SOURCE_DIR}/cmake/WasmCxxShimManifold.cmake)

# Reuse the smoke test's libc++ header detection.
get_filename_component(_clang_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(_llvm_root "${_clang_dir}" DIRECTORY)
set(_libcxx_headers_for_manifold "${_llvm_root}/include/c++/v1")
if(NOT EXISTS "${_libcxx_headers_for_manifold}/vector")
    message(FATAL_ERROR
        "manifold-link: libc++ headers not found at ${_libcxx_headers_for_manifold}.")
endif()

# `-isystem` chain — caller responsibility per the helper's docstring.
# Each `-isystem PATH` pair MUST be wrapped in `SHELL:` or CMake's
# compile-flag deduplication collapses repeat `-isystem` flags
# together (only the first path is recognized; the rest are treated
# as linker inputs and produce "input unused" warnings — confusing
# because the build still attempts to compile but with the wrong
# search path).
add_compile_options(
    "SHELL:-isystem ${CMAKE_SOURCE_DIR}/test/smoke/include"          # __config_site override
    "SHELL:-isystem ${CMAKE_SOURCE_DIR}/test/manifold-link/include"  # <mutex> stub etc.
    "SHELL:-isystem ${_libcxx_headers_for_manifold}"
    "SHELL:-isystem ${CMAKE_SOURCE_DIR}/libm/include"
    "SHELL:-isystem ${CMAKE_SOURCE_DIR}/libc/include"
)

# FetchContent + patches + options + base compile flags.
wasm_cxx_shim_add_manifold()

# ---- Probe: tiny C TU calling manifold's C API ----

set(_probe_obj         "${CMAKE_CURRENT_BINARY_DIR}/probe.c.obj")
set(_libcxx_extras_obj "${CMAKE_CURRENT_BINARY_DIR}/libcxx-extras.cpp.obj")
set(_probe_wasm        "${CMAKE_CURRENT_BINARY_DIR}/manifold-link-probe.wasm")

# Compile libcxx-extras.cpp — the libc++ src/memory.cpp + new_helpers.cpp
# stubs that manifold needs (shared_ptr machinery + std::nothrow).
# Scoped to this test, NOT part of the main libcxx component. See the
# file's preamble for the why.
add_custom_command(
    OUTPUT  ${_libcxx_extras_obj}
    COMMAND ${CMAKE_CXX_COMPILER}
        --target=wasm32-unknown-unknown
        -std=c++17 -Os -fno-exceptions -fno-rtti -nostdlib -nostdinc++
        -isystem ${CMAKE_SOURCE_DIR}/test/smoke/include
        -isystem ${CMAKE_SOURCE_DIR}/test/manifold-link/include
        -isystem ${_libcxx_headers_for_manifold}
        -isystem ${CMAKE_SOURCE_DIR}/libm/include
        -isystem ${CMAKE_SOURCE_DIR}/libc/include
        -c ${CMAKE_CURRENT_SOURCE_DIR}/libcxx-extras.cpp
        -o ${_libcxx_extras_obj}
    DEPENDS libcxx-extras.cpp
    COMMENT "manifold-link: compiling libcxx-extras.cpp"
    VERBATIM
)

add_custom_command(
    OUTPUT  ${_probe_obj}
    COMMAND ${CMAKE_C_COMPILER}
        --target=wasm32-unknown-unknown
        -Os -nostdlib
        -isystem ${manifold_SOURCE_DIR}/bindings/c/include
        -isystem ${CMAKE_SOURCE_DIR}/libc/include
        -c ${CMAKE_CURRENT_SOURCE_DIR}/probe.c
        -o ${_probe_obj}
    DEPENDS probe.c
    COMMENT "manifold-link: compiling probe.c"
    VERBATIM
)

# Get the manifold C-API library's archive path. manifold's CMake
# names it `manifoldc` (or similar — verify).
add_custom_command(
    OUTPUT  ${_probe_wasm}
    COMMAND ${WASM_CXX_SHIM_WASM_LD}
        --no-entry
        --export=probe_run
        ${_probe_obj}
        ${_libcxx_extras_obj}
        $<TARGET_FILE:manifoldc>
        $<TARGET_FILE:manifold>
        $<TARGET_FILE:Clipper2>
        $<TARGET_FILE:wasm-cxx-shim-libcxx>
        $<TARGET_FILE:wasm-cxx-shim-libc>
        $<TARGET_FILE:wasm-cxx-shim-libm>
        -o ${_probe_wasm}
    DEPENDS ${_probe_obj}
            ${_libcxx_extras_obj}
            manifoldc manifold Clipper2
            wasm-cxx-shim-libc wasm-cxx-shim-libm wasm-cxx-shim-libcxx
    COMMENT "manifold-link: linking probe.wasm"
    VERBATIM
)

add_custom_target(manifold-link-probe ALL DEPENDS ${_probe_wasm})

# ---- Tests ----

if(WASM_OBJDUMP_EXECUTABLE)
    add_test(NAME manifold_link_imports_check
        COMMAND bash ${CMAKE_SOURCE_DIR}/test/smoke/check-imports.sh
            ${WASM_OBJDUMP_EXECUTABLE}
            ${_probe_wasm}
    )
endif()

# Size budget. Current: ~340 KB; budget 450 KB to absorb minor growth
# (manifold's library code is the dominant contributor; a meaningful
# library bump may legitimately push past this).
add_test(NAME manifold_link_size_budget
    COMMAND bash ${CMAKE_SOURCE_DIR}/test/smoke/check-size.sh
        ${_probe_wasm}
        460800
)

find_program(_manifold_link_node NAMES node nodejs)
if(_manifold_link_node)
    add_test(NAME manifold_link_run
        COMMAND ${_manifold_link_node}
            ${CMAKE_CURRENT_SOURCE_DIR}/run.mjs
            ${_probe_wasm}
    )
endif()
