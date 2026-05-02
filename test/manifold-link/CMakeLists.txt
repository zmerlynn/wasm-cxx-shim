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
# Carry-patches under patches/ are applied via FetchContent's
# PATCH_COMMAND. Each patch corresponds to a specific upstream-side
# fix that hasn't been merged yet; document the upstream PR (or its
# absence) in the patch file's header.

include(FetchContent)

# Pin manifold version explicitly. Bumping this is a deliberate act
# tracked in this repo's git history. Clipper2 (and any other deps
# manifold pulls via its own FetchContent) come from manifold's
# pinned versions.
set(_manifold_link_manifold_tag "v3.4.1")

# Pre-declare Clipper2 with our patch BEFORE manifold's CMake runs.
# CMake's first-declaration-wins makes manifold's own
# FetchContent_Declare(Clipper2 ...) silently ignored, so our pinned
# version + patch are used. We must match manifold's exact GIT_TAG
# (the SHA, NOT the Clipper2_2.0.1 release tag) and SOURCE_SUBDIR
# CPP — see manifold's cmake/manifoldDeps.cmake for the canonical
# declaration. If manifold bumps its Clipper2 pin, this SHA must move
# in lockstep or our pre-declaration shadows manifold's intent.
FetchContent_Declare(Clipper2
    GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git
    GIT_TAG        46f639177fe418f9689e8ddb74f08a870c71f5b4
    SOURCE_SUBDIR  CPP
    PATCH_COMMAND  git apply --ignore-whitespace -p0
                   ${CMAKE_CURRENT_SOURCE_DIR}/patches/0001-clipper2-strip-iostream.patch
    UPDATE_DISCONNECTED TRUE
)

FetchContent_Declare(manifold
    GIT_REPOSITORY https://github.com/elalish/manifold.git
    GIT_TAG        ${_manifold_link_manifold_tag}
    GIT_SHALLOW    TRUE
    PATCH_COMMAND  git apply --ignore-whitespace -p0
                   ${CMAKE_CURRENT_SOURCE_DIR}/patches/0002-manifold-ifdef-iostream.patch
                   ${CMAKE_CURRENT_SOURCE_DIR}/patches/0003-manifold-test-main-ifdef-filesystem.patch
    UPDATE_DISCONNECTED TRUE
)

# Manifold's options: turn off everything we can. We want the C API
# library + nothing else — no tests, no examples, no Python/JS bindings,
# no parallelism (we don't have threads), and no exceptions/RTTI.
set(MANIFOLD_TEST OFF CACHE BOOL "" FORCE)
set(MANIFOLD_CBIND ON CACHE BOOL "" FORCE)
set(MANIFOLD_PYBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_JSBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PAR OFF CACHE BOOL "" FORCE)  # serial backend (no TBB)
set(MANIFOLD_EXCEPTIONS OFF CACHE BOOL "" FORCE)
set(MANIFOLD_DEBUG OFF CACHE BOOL "" FORCE)
set(MANIFOLD_EXPORT OFF CACHE BOOL "" FORCE)  # no assimp / no meshio

# Clipper2's options (when pulled via FetchContent, its CMakeLists
# defaults Tests/Utils/Examples ON; they pull in <sys/types.h>,
# <sys/stat.h>, googletest, etc. that we don't ship).
set(CLIPPER2_TESTS    OFF CACHE BOOL "" FORCE)
set(CLIPPER2_UTILS    OFF CACHE BOOL "" FORCE)
set(CLIPPER2_EXAMPLES OFF CACHE BOOL "" FORCE)

# Reuse the smoke test's libc++ header detection.
get_filename_component(_clang_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(_llvm_root "${_clang_dir}" DIRECTORY)
set(_libcxx_headers_for_manifold "${_llvm_root}/include/c++/v1")
if(NOT EXISTS "${_libcxx_headers_for_manifold}/vector")
    message(FATAL_ERROR
        "manifold-link: libc++ headers not found at ${_libcxx_headers_for_manifold}.")
endif()

# Pre-emptively configure manifold's wasm-friendly compile flags. Real
# upstream PR for MANIFOLD_WASM_FREESTANDING=ON would make this a
# single switch; for now we set the underlying flags directly.
# `add_compile_options` propagates to subdirectories — manifold's
# build (added via FetchContent_MakeAvailable) inherits these.
add_compile_options(
    -fno-exceptions
    -fno-rtti
    -nostdlib
    -nostdinc++
    # Strip manifold's iostream-using OBJ I/O paths. The patch
    # at patches/0002-manifold-ifdef-iostream.patch wraps them
    # in `#ifndef MANIFOLD_NO_IOSTREAM`. Future upstream PR can
    # use the same macro name for compatibility.
    -DMANIFOLD_NO_IOSTREAM=1
    # Each `-isystem PATH` pair MUST be wrapped in `SHELL:` or CMake's
    # compile-flag deduplication collapses repeat `-isystem` flags
    # together (only the first path is recognized; the rest are treated
    # as linker inputs and produce "input unused" warnings — confusing
    # because the build still attempts to compile but with the wrong
    # search path).
    "SHELL:-isystem ${CMAKE_SOURCE_DIR}/test/smoke/include"          # __config_site override
    "SHELL:-isystem ${CMAKE_SOURCE_DIR}/test/manifold-link/include"  # <mutex> stub etc.
    "SHELL:-isystem ${_libcxx_headers_for_manifold}"
    "SHELL:-isystem ${CMAKE_SOURCE_DIR}/libm/include"
    "SHELL:-isystem ${CMAKE_SOURCE_DIR}/libc/include"
)

FetchContent_MakeAvailable(Clipper2 manifold)

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

find_program(_manifold_link_node NAMES node nodejs)
if(_manifold_link_node)
    add_test(NAME manifold_link_run
        COMMAND ${_manifold_link_node}
            ${CMAKE_CURRENT_SOURCE_DIR}/run.mjs
            ${_probe_wasm}
    )
endif()
