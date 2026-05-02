# tools/wasm-test-harness — minimal test harness for running consumer
# test suites on wasm32-unknown-unknown via the shim.
#
# Built as a static archive that consumer test wasms link against.
# Adapters/ contains translation headers for specific consumer test
# frameworks (currently: GoogleTest).
#
# Target name `wasm-cxx-shim-test-harness` deliberately diverges from
# the shim libraries' `wasm-cxx-shim-libc/-libm/-libcxx` naming: this
# is a tools/ artifact, not a shim component, so it doesn't get a
# `wasm-cxx-shim::` ALIAS and isn't part of the find_package surface.
# Consumer downstreams shouldn't link it directly — only our own test
# integrations (`test/manifold-tests/`, etc.) do.

add_library(wasm-cxx-shim-test-harness STATIC
    src/harness.c
)

target_include_directories(wasm-cxx-shim-test-harness PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_options(wasm-cxx-shim-test-harness PRIVATE
    -Os
)

# Self-tests for the harness primitives. Lightweight (C-only, no
# libcxx), so always built when WASM_CXX_SHIM_BUILD_TESTS is on
# (i.e. when the rest of the test suite is being run).
if(WASM_CXX_SHIM_BUILD_TESTS)
    add_subdirectory(test)
endif()
