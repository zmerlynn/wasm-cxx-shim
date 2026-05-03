# cmake/check-manifold-helper.cmake
#
# Source-tree API smoke for the wasm_cxx_shim_add_manifold() helper.
# Loads the helper module via include() and asserts its surface is
# well-formed: command registered, default tags populated, shipped
# patch files resolve to existing paths.
#
# Doesn't actually call the helper (that would FetchContent; expensive
# + we already exercise the default-args path via test/manifold-link/).
# This is the static-API smoke from the PR-#10 review notes.
#
# Usage (typically invoked by ctest via cmake -P):
#     cmake -P check-manifold-helper.cmake <helper-path>

if(CMAKE_ARGC LESS 4)
    message(FATAL_ERROR
        "usage: cmake -P ${CMAKE_CURRENT_LIST_FILE} <path-to-WasmCxxShimManifold.cmake>")
endif()

# CMAKE_ARGV0=cmake, ARGV1=-P, ARGV2=this script, ARGV3=helper path.
set(_helper_path "${CMAKE_ARGV3}")

if(NOT EXISTS "${_helper_path}")
    message(FATAL_ERROR "helper module does not exist: ${_helper_path}")
endif()

include("${_helper_path}")

# Command should be registered.
if(NOT COMMAND wasm_cxx_shim_add_manifold)
    message(FATAL_ERROR
        "wasm_cxx_shim_add_manifold() command not registered after include")
endif()

# Default tag should be non-empty (catches accidental nuking + typos).
if(NOT _wasm_cxx_shim_manifold_default_manifold_tag)
    message(FATAL_ERROR "default manifold tag is empty")
endif()

# Shipped carry-patch must resolve. Catches "file moved" / "captured
# the wrong CMAKE_CURRENT_LIST_DIR" / "install rule missed a file".
set(_p "${_wasm_cxx_shim_manifold_helper_dir}/manifold-patches/0001-manifold-no-iostream.patch")
if(NOT EXISTS "${_p}")
    message(FATAL_ERROR "shipped patch missing: ${_p}")
endif()

message(STATUS
    "helper API smoke OK: default manifold pin=${_wasm_cxx_shim_manifold_default_manifold_tag}, "
    "carry-patch resolved")
