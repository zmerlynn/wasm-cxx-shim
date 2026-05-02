# test/consumer/external — a tiny standalone CMake project that
# consumes wasm-cxx-shim via find_package + the installed toolchain
# file. Driven by run-consumer-test.sh in the parent dir.

cmake_minimum_required(VERSION 3.25)
project(wasm-cxx-shim-consumer-test
    VERSION 0.0.0
    LANGUAGES C
)

find_package(wasm-cxx-shim REQUIRED COMPONENTS libc libm)

# Tiny C TU that exercises symbols from BOTH libc (malloc/free,
# memset) and libm (sqrt). Linking should resolve everything in-wasm.
add_library(consumer_probe STATIC consumer_probe.c)
target_link_libraries(consumer_probe PRIVATE
    wasm-cxx-shim::libc
    wasm-cxx-shim::libm
)

# Emit a wasm by linking the probe + shim libs through wasm-ld. We
# don't use add_executable() because CMake's wasm-executable handling
# is fragile across versions; calling wasm-ld directly is what every
# real consumer does.
set(_consumer_obj  $<TARGET_OBJECTS:consumer_probe>)
set(_consumer_wasm ${CMAKE_BINARY_DIR}/consumer.wasm)

# WASM_CXX_SHIM_WASM_LD comes from the toolchain file (cached var).
add_custom_command(
    OUTPUT  ${_consumer_wasm}
    COMMAND ${WASM_CXX_SHIM_WASM_LD}
        --no-entry
        --export=probe_run
        ${_consumer_obj}
        $<TARGET_FILE:wasm-cxx-shim::libc>
        $<TARGET_FILE:wasm-cxx-shim::libm>
        -o ${_consumer_wasm}
    DEPENDS consumer_probe
            $<TARGET_FILE:wasm-cxx-shim::libc>
            $<TARGET_FILE:wasm-cxx-shim::libm>
    VERBATIM
)

add_custom_target(consumer ALL DEPENDS ${_consumer_wasm})
