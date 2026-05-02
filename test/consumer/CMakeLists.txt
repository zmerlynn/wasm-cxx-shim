# test/consumer — driver for the find_package(wasm-cxx-shim) install
# round-trip.
#
# This is part of the parent build (so a single `ctest` run can
# exercise it), but the actual VALIDATION happens in the ctest entry
# below: a shell script that installs the shim to a temp prefix and
# then runs an EXTERNAL cmake invocation against it. The external
# invocation simulates what a real downstream consumer would do.
#
# An add_subdirectory + find_package combo would test the source
# tree, not the install tree, and would mask install-rule bugs (e.g.,
# missing INSTALL_INTERFACE generator expressions, header dirs that
# don't exist post-install, etc.).

# Locate the runner. test/smoke and test/consumer share the imports check.
find_program(NODE_EXECUTABLE NAMES node nodejs)
find_program(WASM_OBJDUMP_EXECUTABLE wasm-objdump)

set(_consumer_test_src "${CMAKE_CURRENT_SOURCE_DIR}/external")
set(_consumer_test_run "${CMAKE_CURRENT_SOURCE_DIR}/run-consumer-test.sh")

# Validate the install round-trip and the resulting consumer build.
# Inputs the script needs:
#   shim build dir + config (so it can `cmake --install`)
#   external consumer source dir
#   wasm-objdump (for imports check; optional)
add_test(NAME consumer_install_and_build
    COMMAND bash ${_consumer_test_run}
        ${CMAKE_BINARY_DIR}                    # shim build dir
        ${_consumer_test_src}                  # external consumer source
        ${CMAKE_BINARY_DIR}/test/consumer-out  # output dir
        ${CMAKE_CURRENT_SOURCE_DIR}            # for fixtures (toolchain etc)
)

# Don't make this test depend on smoke artifacts — it runs install
# itself, which depends on the build artifacts via CMake's normal rules.
