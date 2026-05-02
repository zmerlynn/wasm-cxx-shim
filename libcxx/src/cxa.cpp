// libcxx/src/cxa.cpp — Itanium C++ ABI runtime support stubs.
//
// Documented in libcxx/README.md. None of these does any "real" work
// in the libc++abi sense — they're the minimum bodies that satisfy
// linker references from libc++ headers and from compiler-emitted code.
//
// Reference (real implementations we're stubbing):
//   __cxa_atexit:    https://github.com/llvm/llvm-project/blob/main/libcxxabi/src/cxa_aux_runtime.cpp
//   __cxa_throw:     https://github.com/llvm/llvm-project/blob/main/libcxxabi/src/cxa_exception.cpp
//   __cxa_pure_virtual: https://github.com/llvm/llvm-project/blob/main/libcxxabi/src/cxa_virtual.cpp
// Itanium C++ ABI spec for these symbols:
//   https://itanium-cxx-abi.github.io/cxx-abi/abi.html

extern "C" {

// __cxa_atexit registers a destructor for a static-storage-duration
// object. On wasm there's no process exit and we never run the
// destructors, so we acknowledge the registration ("success") and
// forget. Static destructor order is "never runs" — document loudly
// in libcxx/README.md.
int __cxa_atexit(void (*/*func*/)(void*), void* /*arg*/, void* /*dso_handle*/) {
    return 0;
}

// Called when a pure virtual function is invoked through a vtable —
// always a programming error (e.g., calling a virtual from a base-class
// constructor before the derived vtable is set up). Trap.
[[noreturn]] void __cxa_pure_virtual() {
    __builtin_trap();
}

// __cxa_throw is the entry point of `throw expr;`. Real implementations
// stack-unwind to a matching catch. We don't ship an exception runtime;
// any throw becomes an unrecoverable trap. Consumers compile
// `-fno-exceptions` so the throw paths are eliminated at compile time
// and this rarely fires; when it does, it's because of an implicit
// throw from the STL (e.g., std::vector::resize → std::bad_alloc).
[[noreturn]] void __cxa_throw(void* /*thrown_obj*/, void* /*tinfo*/,
                              void (*/*dest*/)(void*)) {
    __builtin_trap();
}

// Called by `new T[n]` when n*sizeof(T) overflows. Real libc++abi throws
// std::bad_array_new_length; we trap.
[[noreturn]] void __cxa_throw_bad_array_new_length() {
    __builtin_trap();
}

// Static-storage-duration handle, referenced by __cxa_atexit call sites
// the compiler emits for thread-local / module-scoped destructors. The
// pointed-to value is opaque; only its address matters.
void* __dso_handle = nullptr;

}  // extern "C"
