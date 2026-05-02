# libc — minimal C runtime subset for wasm32-unknown-unknown.
#
# Independently buildable: a consumer can `add_subdirectory(libc)` from this
# repo without involving libm or libcxx.
#
# Provides: malloc/free/calloc/realloc/aligned_alloc + memcpy/memmove/memset/memcmp.
# Plus a synthetic in-wasm sbrk wrapping memory.grow.
# See README.md for symbol list, source provenance, and swap instructions.

set(WASM_CXX_SHIM_LIBC_SOURCES
    # dlmalloc engine + our wrapper + sbrk shim
    src/dlmalloc/dlmalloc.c
    src/dlmalloc/sbrk.c
    # musl mem* (memcpy/memmove/memset/memcmp)
    src/musl/memcpy.c
    src/musl/memmove.c
    src/musl/memset.c
    src/musl/memcmp.c
)

# malloc.c is #include'd from dlmalloc.c, not compiled directly. List it
# in the target so editors index it, but don't add it as a TU.
set_property(SOURCE src/dlmalloc/malloc.c PROPERTY HEADER_FILE_ONLY TRUE)
list(APPEND WASM_CXX_SHIM_LIBC_SOURCES src/dlmalloc/malloc.c)

add_library(wasm-cxx-shim-libc STATIC ${WASM_CXX_SHIM_LIBC_SOURCES})
add_library(wasm-cxx-shim::libc ALIAS wasm-cxx-shim-libc)
# Exported as `wasm-cxx-shim::libc` (not ::wasm-cxx-shim-libc) so the
# imported-target name matches the in-source ALIAS above.
set_target_properties(wasm-cxx-shim-libc PROPERTIES EXPORT_NAME libc)

target_include_directories(wasm-cxx-shim-libc
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_options(wasm-cxx-shim-libc PRIVATE
    -Os
    # Don't let clang turn our memcpy back into a memcpy call (some clang
    # optimizations recognize hand-written memcpy patterns and substitute
    # the libc call, which is exactly what we're providing).
    -fno-builtin
    # dlmalloc relies on type-punning chunks via casts.
    -fno-strict-aliasing
    # Above this size in bytes, musl's mem* uses __builtin_memcpy/memset/etc
    # (which clang lowers to the wasm bulk-memory instruction). Below, the
    # inline byte/word loop is faster. 200 matches wasi-libc.
    -DBULK_MEMORY_THRESHOLD=200
    # Suppress dlmalloc's deliberately portable C; it triggers warnings
    # that reflect upstream conservatism, not bugs.
    -Wno-null-pointer-arithmetic
    -Wno-unused-but-set-variable
    -Wno-expansion-to-defined
    -Wno-pointer-sign
)

# ---- install ----
# Consumers `find_package(wasm-cxx-shim COMPONENTS libc)` and link
# wasm-cxx-shim::libc. The INTERFACE include directory points at the
# per-component subdir under the install prefix (which is independent
# of libm's and libcxx's include directories — keeps components
# swappable). See top-level CMakeLists for the package config glue.
target_include_directories(wasm-cxx-shim-libc INTERFACE
    $<INSTALL_INTERFACE:include/wasm-cxx-shim/libc>
)

# GNUInstallDirs is included once at the top-level CMakeLists; that's
# inherited here. The variables (CMAKE_INSTALL_LIBDIR etc.) are set.

install(TARGETS wasm-cxx-shim-libc
    EXPORT wasm-cxx-shim-libc-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wasm-cxx-shim/libc
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT wasm-cxx-shim-libc-targets
    NAMESPACE   wasm-cxx-shim::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wasm-cxx-shim
)
