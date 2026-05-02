# wasm-cxx-shim — top-level CMake.
#
# Aggregates the three independently-buildable subdirectories (libc, libm,
# libcxx) and exposes them as components under a single `wasm-cxx-shim`
# find_package config. Each subdirectory is also consumable on its own:
# a downstream project that wants only one component can
# `add_subdirectory(libc)` directly without going through this top-level file.

cmake_minimum_required(VERSION 3.25)
project(wasm-cxx-shim
    VERSION 0.2.0
    DESCRIPTION "Minimal C/C++ runtime shim for wasm32-unknown-unknown"
    LANGUAGES C CXX
)

# Force-clear the auto-detected implicit include directories AFTER
# project() — the toolchain file's pre-project() setting gets stomped
# by compiler detection (which writes CMakeCXXCompiler.cmake with the
# auto-detected `${clang_resource_dir}/include`). CMake then injects
# that path via `-isystem` BEFORE every user `-isystem`, breaking
# libc++'s `<cstddef>` → `<stddef.h>` resolution chain
# (libc++'s own stddef.h wrapper gets shadowed by clang's freestanding
# one). Surfaced when integrating manifold via FetchContent
# (manifold uses standard add_library which goes through CMake's
# compile-rule machinery; the smoke test bypassed it via
# add_custom_command).
set(CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES   "")
set(CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "")

# Always emit compile_commands.json so LSPs (clangd, ccls) get the right
# target triple and headers. Without this, an LSP using Apple clang at the
# default target won't recognize __builtin_wasm_memory_grow etc.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Sanity-check the target. The `wasm32` toolchain file sets SYSTEM_NAME=Generic
# and SYSTEM_PROCESSOR=wasm32; that's the expected configuration. Anything
# else (a host-targeted Darwin/Linux/Windows build) is almost certainly a
# mistake — warn loudly so the misconfiguration doesn't reach a confusing
# downstream link error.
if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Generic" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "wasm32"))
    message(WARNING
        "wasm-cxx-shim is intended for wasm32-unknown-unknown targets. "
        "You appear to be configuring for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}. "
        "Pass -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchain-wasm32.cmake "
        "or use the `wasm32` preset (cmake --preset wasm32).")
endif()

option(WASM_CXX_SHIM_BUILD_LIBC   "Build the libc component"   ON)
option(WASM_CXX_SHIM_BUILD_LIBM   "Build the libm component"   ON)
option(WASM_CXX_SHIM_BUILD_LIBCXX "Build the libcxx component" ON)
option(WASM_CXX_SHIM_BUILD_TESTS  "Build the smoke tests"      OFF)
# Heavy / opt-in: clones manifold + Clipper2 + dependencies (multi-MB,
# minutes to build). See test/manifold-link/README.md.
option(WASM_CXX_SHIM_BUILD_MANIFOLD_LINK
       "Build the upstream-manifold link test (Phase 7 precursor)" OFF)

# Install-path conventions (CMAKE_INSTALL_LIBDIR, CMAKE_INSTALL_INCLUDEDIR,
# CMAKE_INSTALL_DATADIR). Included BEFORE add_subdirectory so the
# per-component install rules can use the variables.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(WASM_CXX_SHIM_BUILD_LIBC)
    add_subdirectory(libc)
endif()
if(WASM_CXX_SHIM_BUILD_LIBM)
    add_subdirectory(libm)
endif()
if(WASM_CXX_SHIM_BUILD_LIBCXX)
    add_subdirectory(libcxx)
endif()

# enable_testing() before any subdirectory that calls add_test() — the
# harness self-tests under tools/wasm-test-harness/test/ register ctest
# entries and would be silently dropped if testing weren't enabled
# beforehand.
if(WASM_CXX_SHIM_BUILD_TESTS OR WASM_CXX_SHIM_BUILD_MANIFOLD_LINK)
    enable_testing()
endif()

# Test harness lives under tools/ — built when tests are on, since
# manifold-tests (and any future consumer test integration) link against it.
if(WASM_CXX_SHIM_BUILD_TESTS OR WASM_CXX_SHIM_BUILD_MANIFOLD_LINK)
    add_subdirectory(tools/wasm-test-harness)
endif()

if(WASM_CXX_SHIM_BUILD_TESTS)
    add_subdirectory(test/smoke)
    add_subdirectory(test/consumer)
endif()

if(WASM_CXX_SHIM_BUILD_MANIFOLD_LINK)
    add_subdirectory(test/manifold-link)
    add_subdirectory(test/manifold-tests)
endif()

# ---- find_package(wasm-cxx-shim) support ----
# Each enabled component installed its own targets export above; here we
# install the top-level config + version files so consumers can do
#   find_package(wasm-cxx-shim REQUIRED COMPONENTS libc libm libcxx)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/wasm-cxx-shimConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/WasmCxxShimConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/wasm-cxx-shimConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wasm-cxx-shim
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/wasm-cxx-shimConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/wasm-cxx-shimConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wasm-cxx-shim
)

# Install the toolchain file as a convenience for downstream consumers
# building C++ libraries against this shim. They pass it as
# `-DCMAKE_TOOLCHAIN_FILE=${prefix}/share/wasm-cxx-shim/toolchain-wasm32.cmake`.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain-wasm32.cmake
    DESTINATION ${CMAKE_INSTALL_DATADIR}/wasm-cxx-shim
)
