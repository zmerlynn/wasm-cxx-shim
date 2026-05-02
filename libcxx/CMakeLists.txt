# libcxx — C++ runtime subset for wasm32-unknown-unknown.
#
# Independently buildable. Provides ABI symbols (libc++abi territory)
# and runtime support (libc++ territory) needed to link C++ code that
# uses standard library types.
#
# Only the symbols LLVM does NOT ship for wasm32-unknown-unknown live
# here. Consumers' libc++ headers + 4 libc++ source TUs (bind.cpp /
# hash.cpp / memory.cpp / new_helpers.cpp from their LLVM tree) provide
# the rest.
#
# See README.md for the symbol list, ABI mangling reference, and the
# `__cxa_throw` policy.

set(WASM_CXX_SHIM_LIBCXX_SOURCES
    src/cxa.cpp                    # __cxa_atexit, __cxa_pure_virtual,
                                   # __cxa_throw (trap stub), __dso_handle
    src/operator_new_delete.cpp    # 4 operator new + 3 operator delete
    src/verbose_abort.cpp          # std::__1::__libcpp_verbose_abort
    src/exception.cpp              # std::exception::~exception() + key fns
    src/stubs.cpp                  # placeholder for follow-on adds
)

add_library(wasm-cxx-shim-libcxx STATIC ${WASM_CXX_SHIM_LIBCXX_SOURCES})
add_library(wasm-cxx-shim::libcxx ALIAS wasm-cxx-shim-libcxx)
set_target_properties(wasm-cxx-shim-libcxx PROPERTIES EXPORT_NAME libcxx)

target_compile_options(wasm-cxx-shim-libcxx PRIVATE
    -Os
    -std=c++17
    -fno-exceptions
    -fno-rtti
)

# ---- install ----
# libcxx ships no public headers — it's pure ABI/runtime stubs.
# Consumers bring their own libc++ headers from their LLVM install
# (see test/smoke/CMakeLists.txt for the discovery pattern).
# GNUInstallDirs included at top-level; CMAKE_INSTALL_* vars inherited.

install(TARGETS wasm-cxx-shim-libcxx
    EXPORT wasm-cxx-shim-libcxx-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(EXPORT wasm-cxx-shim-libcxx-targets
    NAMESPACE   wasm-cxx-shim::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wasm-cxx-shim
)

# operator new/delete need an allocator. By default, expect the consumer
# to also link wasm-cxx-shim::libc (for malloc/free), but don't force
# the dependency at the cmake level — that would make this component
# harder to swap. operator_new_delete.cpp declares the malloc/free/
# aligned_alloc prototypes inline rather than including a libc header.
