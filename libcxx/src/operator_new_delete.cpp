// libcxx/src/operator_new_delete.cpp — global operator new/delete.
//
// Reference for the symbol set + signatures we're replacing:
//   https://github.com/llvm/llvm-project/blob/main/libcxx/src/new.cpp
//
// Provides all 7 variants the linker reports as undefined when building
// C++ code that uses STL containers on wasm32-unknown-unknown:
//   4 × operator new   (default, array, align_val, nothrow)
//   3 × operator delete (default, array, align_val)
//
// The array nothrow / align_val nothrow / sized-delete variants are
// not provided in v0.1; consumers haven't reported needing them. Add
// by demand.
//
// Critical: this TU must NOT #include <new> or any other libc++
// header. We don't ship libc++ headers, and a consumer's <new> may
// re-declare std::nothrow_t / std::align_val_t with version-specific
// attributes. Local declarations of `std::nothrow_t` and
// `std::align_val_t` produce the SAME Itanium-mangled symbols (the
// ABI cares about qualified names, not declaration sites).
//
// `malloc`, `free`, and `aligned_alloc` come from libc. We declare
// them inline rather than including a libc header — keeping libcxx
// independent of libc at the CMake level.

#include <stddef.h>  // size_t

extern "C" {
    void* malloc(size_t);
    void  free(void*);
    void* aligned_alloc(size_t alignment, size_t size);
}

namespace std {
    struct nothrow_t {};
    enum class align_val_t : size_t {};
}

// ----------------------------------------------------------------------------
// operator new — throwing variants. The standard says "throw bad_alloc on
// failure"; we don't ship an exception runtime, so we trap. Consumers
// compiling -fno-exceptions get the throw paths stripped, leaving just
// the malloc result.
// ----------------------------------------------------------------------------

void* operator new(size_t size) {
    void* p = malloc(size);
    if (!p) __builtin_trap();
    return p;
}

void* operator new[](size_t size) {
    return ::operator new(size);
}

void* operator new(size_t size, std::align_val_t align) {
    // C11 aligned_alloc requires size to be a multiple of the alignment.
    // Our libc's dlmalloc-backed version does not care, but a consumer
    // may swap in a stricter libc, so round up here. Alignment is a power
    // of two (the standard requires it of align_val_t). Trap on overflow
    // rather than wrap to a short block.
    size_t a = static_cast<size_t>(align);
    if (size > static_cast<size_t>(-1) - (a - 1)) __builtin_trap();
    size_t rounded = (size + (a - 1)) & ~(a - 1);
    void* p = aligned_alloc(a, rounded);
    if (!p) __builtin_trap();
    return p;
}

// ----------------------------------------------------------------------------
// operator new — nothrow variant. Returns null on failure rather than
// throwing.
// ----------------------------------------------------------------------------

void* operator new(size_t size, const std::nothrow_t&) noexcept {
    return malloc(size);
}

// ----------------------------------------------------------------------------
// operator delete. All forward to free(). free(nullptr) is well-defined,
// so no null-check needed.
//
// Both sized and unsized variants must be provided: clang ≥ 14 with C++14+
// emits sized-delete calls (_ZdlPvm / _ZdaPvm) by default. The size info is
// useful to a real allocator that wants to skip its tree lookup; dlmalloc
// doesn't need it, so we ignore the size argument.
// ----------------------------------------------------------------------------

// unsized
void operator delete  (void* p) noexcept                                       { free(p); }
void operator delete[](void* p) noexcept                                       { free(p); }
void operator delete  (void* p,                std::align_val_t) noexcept      { free(p); }
void operator delete[](void* p,                std::align_val_t) noexcept      { free(p); }

// sized (C++14)
void operator delete  (void* p, size_t)                          noexcept      { free(p); }
void operator delete[](void* p, size_t)                          noexcept      { free(p); }
void operator delete  (void* p, size_t,        std::align_val_t) noexcept      { free(p); }
void operator delete[](void* p, size_t,        std::align_val_t) noexcept      { free(p); }
