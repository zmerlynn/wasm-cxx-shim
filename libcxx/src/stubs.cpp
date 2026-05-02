// libcxx/src/stubs.cpp — incremental stubs for symbols the smoke test
// or downstream consumers surface that aren't in the original 31-symbol
// baseline. Keep each addition documented with the symbol name, why we
// need a stub, and what the "real" libc++ behavior is.

#include <stddef.h>

// ----------------------------------------------------------------------------
// std::__1::__next_prime(size_t)
//
// Used by std::unordered_map / unordered_set to pick a hash-bucket count
// that's prime (better hash distribution). libc++'s real version
//   https://github.com/llvm/llvm-project/blob/main/libcxx/src/hash.cpp
// has a 200+-entry small-primes table for fast lookup at small sizes
// plus a sieve-based trial division for large sizes. We provide a tiny
// trial-division implementation: O(√n) per call, only invoked during
// rehash, so the overhead is irrelevant for any realistic workload.
//
// Mangled: _ZNSt3__111__next_primeEm
//
// libc++'s version throws std::overflow_error on N > sentinel. We
// trap instead — consumers compile -fno-exceptions, and our shim's
// __cxa_throw is a trap anyway, so the behavior is equivalent.
// ----------------------------------------------------------------------------

namespace std { inline namespace __1 {

size_t __next_prime(size_t n) {
    if (n <= 2) return 2;
    if (n % 2 == 0) ++n;
    while (true) {
        bool prime = true;
        for (size_t i = 3; i * i <= n; i += 2) {
            if (n % i == 0) { prime = false; break; }
        }
        if (prime) return n;
        n += 2;
        if (n == 0) __builtin_trap();   // overflow → trap
    }
}

}}  // namespace std::__1
