/*
 * <assert.h> — minimal freestanding implementation.
 *
 * Surfaced as a symbol gap by manifold v3.5.0 (disjoint_sets.h
 * includes <cassert> + uses assert()). libc++'s <cassert> falls
 * through to <assert.h> via __has_include; without this file, the
 * macro is undefined and consumer code fails with "use of
 * undeclared identifier 'assert'".
 *
 * Per C standard: <assert.h> is the one libc header whose contents
 * are re-evaluated on every include, so the include guard is
 * deliberately omitted — only the assert macro is conditionally
 * redefined based on NDEBUG.
 */

#undef assert

#ifdef NDEBUG
/* NDEBUG path: expr must be present in the expansion (under `sizeof`,
 * which doesn't evaluate it) so variables/values referenced only inside
 * asserts don't trigger -Wunused-variable / -Wunused-but-set-variable
 * in release builds. Matches glibc/musl/BSD convention. */
#define assert(expr) ((void)sizeof((expr)))
#else
#define assert(expr) ((expr) ? ((void)0) : __builtin_trap())
#endif

/* C11 static_assert keyword maps to _Static_assert (also available
 * in C99 via the keyword in our toolchain). */
#if !defined(__cplusplus) && !defined(static_assert)
#define static_assert _Static_assert
#endif
