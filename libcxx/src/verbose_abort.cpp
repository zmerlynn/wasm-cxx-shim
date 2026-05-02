// libcxx/src/verbose_abort.cpp — std::__1::__libcpp_verbose_abort.
//
// libc++'s assertion-handler entry point. Called from inside libc++
// when an internal invariant fails (out-of-range vector access in
// hardened mode, etc.). Real implementations print a formatted message
// and abort(); we trap.
//
// Reference for the function declaration (so the symbol name + linkage
// match):
//   https://github.com/llvm/llvm-project/blob/main/libcxx/include/__verbose_abort
// Real implementation we're replacing:
//   https://github.com/llvm/llvm-project/blob/main/libcxx/src/verbose_abort.cpp
//
// Critical: this TU does NOT #include <__verbose_abort> or any libc++
// header. The Itanium ABI mangling of this symbol is `noexcept`-
// invariant, so a definition without `noexcept` still satisfies a
// reference from any libc++ version. Including the header would bring
// in version-specific declarations that may noexcept-mismatch this
// definition (LLVM 20+ headers carry `noexcept` unconditionally). The
// header-free pattern works portably across LLVM 16 → main.
//
// Mangled symbol (verified): _ZNSt3__122__libcpp_verbose_abortEPKcz
// Lives in the VERSIONED std::__1:: namespace, NOT the unversioned
// std:: of std::exception::~exception. Get this wrong and the link
// fails with a confusing missing-symbol error.

namespace std { inline namespace __1 {

[[noreturn]]
__attribute__((__format__(__printf__, 1, 2)))
void __libcpp_verbose_abort(const char* /*format*/, ...) noexcept {
    __builtin_trap();
}

}}  // namespace std::__1
