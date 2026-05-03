# test/smoke — end-to-end smoke test.
#
# Compiles smoke.cpp for wasm32-unknown-unknown using:
#   * the consumer's libc++ headers (auto-detected near the chosen clang)
#   * pca006132's __config_site override (test/smoke/include/) which
#     disables threads/filesystem/locale/etc. — without it, libc++
#     headers drag in symbols our shim doesn't and shouldn't ship
#   * libc/libm headers from this repo (for <stdio.h>, <math.h>, etc.)
# Links against the three shim components.
#
# Verifies the resulting .wasm has zero imports and produces the
# expected return value when run under Node.

# Locate libc++ headers from the chosen clang's install. Standard layout:
#   ${clang_dir}/../include/c++/v1/
get_filename_component(_clang_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(_llvm_root "${_clang_dir}" DIRECTORY)
set(_libcxx_headers "${_llvm_root}/include/c++/v1")
if(WASM_CXX_SHIM_LIBCXX_HEADERS)
    set(_libcxx_headers "${WASM_CXX_SHIM_LIBCXX_HEADERS}")
endif()
if(NOT EXISTS "${_libcxx_headers}/vector")
    message(FATAL_ERROR
        "Could not find libc++ headers near ${CMAKE_CXX_COMPILER}.\n"
        "Looked at: ${_libcxx_headers}\n"
        "Set WASM_CXX_SHIM_LIBCXX_HEADERS in the cmake cache to override.")
endif()
message(STATUS "smoke test: libc++ headers at ${_libcxx_headers}")

find_program(NODE_EXECUTABLE NAMES node nodejs)
if(NOT NODE_EXECUTABLE)
    message(WARNING "smoke test: node not found; smoke_run test will be skipped.")
endif()

find_program(WASM_OBJDUMP_EXECUTABLE wasm-objdump)
if(NOT WASM_OBJDUMP_EXECUTABLE)
    message(WARNING "smoke test: wasm-objdump not found; smoke_imports_check will be skipped. (Install wabt: brew/apt install wabt)")
endif()

set(_smoke_obj  "${CMAKE_CURRENT_BINARY_DIR}/smoke.cpp.obj")
set(_smoke_wasm "${CMAKE_CURRENT_BINARY_DIR}/smoke.wasm")

add_custom_command(
    OUTPUT  ${_smoke_obj}
    COMMAND ${CMAKE_CXX_COMPILER}
        --target=wasm32-unknown-unknown
        -std=c++17
        -fno-exceptions
        -fno-rtti
        -nostdlib
        -nostdinc++
        -Os
        # Include order matters:
        #   ours/__config_site override   →  must beat libc++'s baked one
        #   libc++ headers                 →  <vector>, <cmath>, <new>, etc.
        #   libm public headers            →  <math.h> (libc++'s <cmath>
        #                                     does include_next <math.h>)
        #   libc public headers            →  <stdio.h>, <stdlib.h>,
        #                                     <wchar.h>, <string.h>, etc.
        -isystem ${CMAKE_CURRENT_SOURCE_DIR}/include
        -isystem ${_libcxx_headers}
        -isystem ${CMAKE_SOURCE_DIR}/libm/include
        -isystem ${CMAKE_SOURCE_DIR}/libc/include
        -c ${CMAKE_CURRENT_SOURCE_DIR}/smoke.cpp
        -o ${_smoke_obj}
    DEPENDS smoke.cpp
            include/__config_site
            include/__assertion_handler
    COMMENT "smoke: compiling smoke.cpp for wasm32-unknown-unknown"
    VERBATIM
)

add_custom_command(
    OUTPUT  ${_smoke_wasm}
    # Link order: smoke object → libcxx (operator new/delete) → libc
    # (malloc/free/mem*) → libm (sqrt/sin). wasm-ld resolves
    # left-to-right; libcxx's operator new pulls libc's malloc, so libc
    # must come after libcxx.
    COMMAND ${WASM_CXX_SHIM_WASM_LD}
        --no-entry
        --export=run
        ${_smoke_obj}
        $<TARGET_FILE:wasm-cxx-shim-libcxx>
        $<TARGET_FILE:wasm-cxx-shim-libc>
        $<TARGET_FILE:wasm-cxx-shim-libm>
        -o ${_smoke_wasm}
    DEPENDS ${_smoke_obj}
            wasm-cxx-shim-libc
            wasm-cxx-shim-libm
            wasm-cxx-shim-libcxx
    COMMENT "smoke: linking smoke.wasm"
    VERBATIM
)

add_custom_target(smoke ALL DEPENDS ${_smoke_wasm})

if(WASM_OBJDUMP_EXECUTABLE)
    add_test(NAME smoke_imports_check
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/check-imports.sh
            ${WASM_OBJDUMP_EXECUTABLE}
            ${_smoke_wasm}
    )
endif()

# Size budget. Current: ~13.5 KB; budget 20 KB to absorb minor growth.
add_test(NAME smoke_size_budget
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/check-size.sh
        ${_smoke_wasm}
        20480
)

if(NODE_EXECUTABLE)
    add_test(NAME smoke_run
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.mjs ${_smoke_wasm}
    )
endif()
