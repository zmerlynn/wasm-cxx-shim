# test/negative-link — assert that calling unimplemented libc functions
# produces a link-time error.
#
# This is a NEGATIVE test: success means the build deliberately fails.
# The shim claims that calling fopen / system / clock_gettime / open /
# read / etc. yields a link error (or compile error, for headers we
# don't ship that don't even declare them); this test makes that claim
# machine-verified instead of merely-asserted-in-the-README.
#
# What it catches: someone accidentally adding an implementation for
# an out-of-scope function. Without this test, that regression would
# slip into a release and a downstream consumer would be the one to
# notice their wasm now has a syscall they didn't expect.

add_test(NAME negative_link_unimplemented
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/expect-link-failure.sh
        ${CMAKE_C_COMPILER}
        ${WASM_CXX_SHIM_WASM_LD}
        ${CMAKE_CURRENT_SOURCE_DIR}/uses_unimplemented.c
        ${CMAKE_SOURCE_DIR}/libc/include
        $<TARGET_FILE:wasm-cxx-shim-libc>
)

# The test itself doesn't produce a wasm artifact (its happy path is
# link-failure), so no add_custom_target / no size budget. It also
# doesn't need a Node runner.
