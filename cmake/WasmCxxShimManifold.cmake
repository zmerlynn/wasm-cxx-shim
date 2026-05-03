# cmake/WasmCxxShimManifold.cmake
#
# Helper for consumers building manifold (elalish/manifold) on top of
# wasm-cxx-shim for the wasm32-unknown-unknown target. Captures the
# high-change-rate parts of the integration cocktail (FetchContent
# pins, patch application, manifold's CMake options) that drift fast
# across manifold versions. Lower-change-rate parts (the libc++
# `__config_site` override, the `<mutex>` stub header, and
# `libcxx-extras.cpp`) stay scoped under the shim's
# `test/manifold-link/` for libc++-source-drift insulation reasons
# documented in CLAUDE.md; the consumer copies those files into their
# own tree (or rolls equivalents).
#
# Loaded automatically by `find_package(wasm-cxx-shim)`. Source-tree
# consumers (`add_subdirectory(wasm-cxx-shim)`) can include explicitly:
#     include(${wasm-cxx-shim_SOURCE_DIR}/cmake/WasmCxxShimManifold.cmake)
#
# Usage:
#
#     find_package(wasm-cxx-shim REQUIRED COMPONENTS libc libm libcxx)
#     wasm_cxx_shim_add_manifold()  # uses tested defaults (see CHANGELOG)
#
# With overrides:
#
#     wasm_cxx_shim_add_manifold(
#         MANIFOLD_GIT_TAG       <ref>      # tag or SHA; default = shim's tested pin
#         CLIPPER2_GIT_TAG       <ref>      # default = matching SHA
#         EXTRA_MANIFOLD_PATCHES <p>...     # additional patches to apply
#         EXTRA_CLIPPER2_PATCHES <p>...
#         SKIP_BUILTIN_PATCHES              # opt out of shim-shipped patches
#     )
#
# After the call, the `manifold`, `manifoldc`, and `Clipper2` CMake
# targets exist with the right manifold/Clipper2 CMake options applied
# (MANIFOLD_TEST=OFF, MANIFOLD_PAR=OFF, etc.) and ambient
# `add_compile_options` set (-fno-exceptions / -fno-rtti / -nostdlib /
# -nostdinc++ + the MANIFOLD_NO_IOSTREAM/FILESYSTEM/PAR=-1 macros +
# CLIPPER2_MAX_DECIMAL_PRECISION=8).
#
# The consumer is responsible for:
#   * the `-isystem` chain (libc++ headers, the `__config_site`
#     override, the `<mutex>` stub directory, and the shim's
#     libm/libc include paths);
#   * compiling `libcxx-extras.cpp` (or equivalent) and adding it to
#     the link;
#   * the final `wasm-ld` invocation with `--no-entry --export=...`.
#
# The shim's `test/manifold-link/CMakeLists.txt` is a worked example
# that consumes this helper.

include_guard(GLOBAL)

# Resolve our own location so the helper finds its shipped patches
# regardless of whether it's loaded from the source tree or from an
# installed package config dir.
set(_wasm_cxx_shim_manifold_helper_dir "${CMAKE_CURRENT_LIST_DIR}")

# Tested-pin defaults. Bumped when the shim cuts a new release that
# verifies a new manifold/Clipper2 combination. Source of truth: this
# file. Changes here must be paired with a CHANGELOG.md entry + a
# version bump.
set(_wasm_cxx_shim_manifold_default_manifold_tag "v3.4.1"
    CACHE STRING
    "Default manifold pin used by wasm_cxx_shim_add_manifold()")
set(_wasm_cxx_shim_manifold_default_clipper2_tag "46f639177fe418f9689e8ddb74f08a870c71f5b4"
    CACHE STRING
    "Default Clipper2 pin (must match manifold's pin for the manifold tag)")

function(wasm_cxx_shim_add_manifold)
    cmake_parse_arguments(_WCSAM
        "SKIP_BUILTIN_PATCHES"
        "MANIFOLD_GIT_TAG;CLIPPER2_GIT_TAG"
        "EXTRA_MANIFOLD_PATCHES;EXTRA_CLIPPER2_PATCHES"
        ${ARGN})

    if(_WCSAM_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "wasm_cxx_shim_add_manifold: unrecognized arguments: ${_WCSAM_UNPARSED_ARGUMENTS}")
    endif()

    if(NOT _WCSAM_MANIFOLD_GIT_TAG)
        set(_WCSAM_MANIFOLD_GIT_TAG "${_wasm_cxx_shim_manifold_default_manifold_tag}")
    endif()
    if(NOT _WCSAM_CLIPPER2_GIT_TAG)
        set(_WCSAM_CLIPPER2_GIT_TAG "${_wasm_cxx_shim_manifold_default_clipper2_tag}")
    endif()

    # Assemble patch lists. Builtin patches first, then user extras —
    # caller can append context-aware patches that depend on builtin
    # state, or replace builtins via SKIP_BUILTIN_PATCHES + EXTRA_*.
    set(_clipper2_patches "")
    set(_manifold_patches "")
    if(NOT _WCSAM_SKIP_BUILTIN_PATCHES)
        list(APPEND _clipper2_patches
            "${_wasm_cxx_shim_manifold_helper_dir}/manifold-patches/0001-clipper2-strip-iostream.patch")
        list(APPEND _manifold_patches
            "${_wasm_cxx_shim_manifold_helper_dir}/manifold-patches/0002-manifold-ifdef-iostream.patch"
            "${_wasm_cxx_shim_manifold_helper_dir}/manifold-patches/0003-manifold-test-main-ifdef-filesystem.patch")
    endif()
    list(APPEND _clipper2_patches ${_WCSAM_EXTRA_CLIPPER2_PATCHES})
    list(APPEND _manifold_patches ${_WCSAM_EXTRA_MANIFOLD_PATCHES})

    include(FetchContent)

    # Pre-declare Clipper2 BEFORE manifold's own FetchContent_Declare
    # runs. CMake's first-declaration-wins makes manifold's silently
    # ignored, so our pin + patches are used. Match manifold's
    # SOURCE_SUBDIR (CPP); see manifold's cmake/manifoldDeps.cmake.
    set(_clipper2_decl_args
        GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git
        GIT_TAG        ${_WCSAM_CLIPPER2_GIT_TAG}
        SOURCE_SUBDIR  CPP
        UPDATE_DISCONNECTED TRUE)
    if(_clipper2_patches)
        list(APPEND _clipper2_decl_args
            PATCH_COMMAND git apply --ignore-whitespace -p0 ${_clipper2_patches})
    endif()
    FetchContent_Declare(Clipper2 ${_clipper2_decl_args})

    set(_manifold_decl_args
        GIT_REPOSITORY https://github.com/elalish/manifold.git
        GIT_TAG        ${_WCSAM_MANIFOLD_GIT_TAG}
        GIT_SHALLOW    TRUE
        UPDATE_DISCONNECTED TRUE)
    if(_manifold_patches)
        list(APPEND _manifold_decl_args
            PATCH_COMMAND git apply --ignore-whitespace -p0 ${_manifold_patches})
    endif()
    FetchContent_Declare(manifold ${_manifold_decl_args})

    # Manifold + Clipper2 CMake options. Set BEFORE
    # FetchContent_MakeAvailable. Most are about turning off things
    # that would otherwise pull in dependencies the shim doesn't have
    # (Python bindings, JS bindings, googletest, threading backends).
    set(MANIFOLD_TEST       OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_CBIND      ON  CACHE BOOL "" FORCE)
    set(MANIFOLD_PYBIND     OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_JSBIND     OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_PAR        OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_EXCEPTIONS OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_DEBUG      OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_EXPORT     OFF CACHE BOOL "" FORCE)
    set(CLIPPER2_TESTS      OFF CACHE BOOL "" FORCE)
    set(CLIPPER2_UTILS      OFF CACHE BOOL "" FORCE)
    set(CLIPPER2_EXAMPLES   OFF CACHE BOOL "" FORCE)

    # Compile flags applied to manifold + Clipper2 sources via the
    # ambient add_compile_options. These propagate to all
    # subdirectories added afterward, which is what we want for a
    # wasm32 build (consumer code wants the same flags).
    add_compile_options(
        -fno-exceptions
        -fno-rtti
        -nostdlib
        -nostdinc++
        -DMANIFOLD_NO_IOSTREAM=1
        -DMANIFOLD_NO_FILESYSTEM=1
        -DMANIFOLD_PAR=-1
        -DCLIPPER2_MAX_DECIMAL_PRECISION=8
    )

    FetchContent_MakeAvailable(Clipper2 manifold)
endfunction()
