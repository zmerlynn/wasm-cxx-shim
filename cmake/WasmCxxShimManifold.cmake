# cmake/WasmCxxShimManifold.cmake
#
# Helper for consumers building manifold (elalish/manifold) on top of
# wasm-cxx-shim for the wasm32-unknown-unknown target. Captures the
# high-change-rate parts of the integration cocktail (FetchContent
# pin, manifold's CMake options) that drift fast across manifold
# versions. Lower-change-rate parts
# (the libc++ `__config_site` override, the `<mutex>` stub header,
# and `libcxx-extras.cpp`) stay scoped under the shim's
# `test/manifold-link/` for libc++-source-drift insulation reasons
# documented in CLAUDE.md; the consumer copies those files into
# their own tree (or rolls equivalents).
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
#         EXTRA_MANIFOLD_PATCHES <p>...     # caller-supplied patches to apply
#     )
#
# After the call, the `manifold`, `manifoldc`, and `Clipper2` CMake
# targets exist with the right manifold/Clipper2 CMake options
# applied (MANIFOLD_TEST=OFF, MANIFOLD_PAR=OFF, MANIFOLD_NO_IOSTREAM=ON,
# etc.) and ambient `add_compile_options` set (-fno-exceptions /
# -fno-rtti / -nostdlib / -nostdinc++). `Clipper2` is created via
# manifold's nested FetchContent_MakeAvailable (this helper does
# not pre-declare it).
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
#
# ---
#
# Manifold's `MANIFOLD_NO_IOSTREAM` option (added upstream in #1690)
# does the work: defining it ON propagates `MANIFOLD_NO_IOSTREAM` /
# `MANIFOLD_NO_FILESYSTEM` / `CLIPPER2_NO_IOSTREAM` as PUBLIC compile
# defs through manifold's and Clipper2's targets. The shim just sets
# the option ON. No carry-patches are shipped for default-pin builds.

include_guard(GLOBAL)

# Tested-pin default. Bumped when the shim cuts a new release that
# verifies a new manifold combination. Source of truth: this file.
# Changes here must be paired with a CHANGELOG.md entry + a version
# bump.
set(_wasm_cxx_shim_manifold_default_manifold_tag
    "3ce9622b"
    CACHE STRING
    "Default manifold pin used by wasm_cxx_shim_add_manifold()")

function(wasm_cxx_shim_add_manifold)
    cmake_parse_arguments(_WCSAM
        ""
        "MANIFOLD_GIT_TAG"
        "EXTRA_MANIFOLD_PATCHES"
        ${ARGN})

    if(_WCSAM_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "wasm_cxx_shim_add_manifold: unrecognized arguments: ${_WCSAM_UNPARSED_ARGUMENTS}")
    endif()

    if(NOT _WCSAM_MANIFOLD_GIT_TAG)
        set(_WCSAM_MANIFOLD_GIT_TAG "${_wasm_cxx_shim_manifold_default_manifold_tag}")
    endif()

    include(FetchContent)

    # Manifold owns its Clipper2 declaration (manifoldDeps.cmake) and
    # propagates CLIPPER2_NO_IOSTREAM=ON to the bundled Clipper2 when
    # MANIFOLD_NO_IOSTREAM is on. We don't pre-declare Clipper2 here.
    set(_manifold_decl_args
        GIT_REPOSITORY https://github.com/elalish/manifold.git
        GIT_TAG        ${_WCSAM_MANIFOLD_GIT_TAG}
        UPDATE_DISCONNECTED TRUE)
    if(_WCSAM_EXTRA_MANIFOLD_PATCHES)
        # `git apply` flag set matches manifold-csg's proven recipe:
        # --ignore-whitespace + --whitespace=nowarn for cross-platform
        # robustness (CRLF, mixed indentation).
        list(APPEND _manifold_decl_args
            PATCH_COMMAND git apply --ignore-whitespace --whitespace=nowarn ${_WCSAM_EXTRA_MANIFOLD_PATCHES})
    endif()
    FetchContent_Declare(manifold ${_manifold_decl_args})

    # Manifold + Clipper2 CMake options. Set BEFORE
    # FetchContent_MakeAvailable. MANIFOLD_NO_IOSTREAM=ON is the
    # load-bearing one — manifold's option chain (added by the
    # carry-patch) propagates it to MANIFOLD_NO_FILESYSTEM and
    # CLIPPER2_NO_IOSTREAM as PUBLIC compile defs.
    set(MANIFOLD_NO_IOSTREAM ON  CACHE BOOL "" FORCE)
    set(MANIFOLD_TEST        OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_CBIND       ON  CACHE BOOL "" FORCE)
    set(MANIFOLD_PYBIND      OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_JSBIND      OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_PAR         OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_EXCEPTIONS  OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_DEBUG       OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_EXPORT      OFF CACHE BOOL "" FORCE)
    set(CLIPPER2_TESTS       OFF CACHE BOOL "" FORCE)
    set(CLIPPER2_UTILS       OFF CACHE BOOL "" FORCE)
    set(CLIPPER2_EXAMPLES    OFF CACHE BOOL "" FORCE)

    # Compile flags applied to manifold + Clipper2 sources via the
    # ambient add_compile_options. These propagate to all
    # subdirectories added afterward, which is what we want for a
    # wasm32 build (consumer code wants the same flags). The
    # NO_IOSTREAM/NO_FILESYSTEM macros are now set as PUBLIC compile
    # defs by manifold itself (via the carry-patch); we no longer
    # need to add them here.
    add_compile_options(
        -fno-exceptions
        -fno-rtti
        -nostdlib
        -nostdinc++
        -DMANIFOLD_PAR=-1
        -DCLIPPER2_MAX_DECIMAL_PRECISION=8
    )

    FetchContent_MakeAvailable(manifold)
endfunction()
