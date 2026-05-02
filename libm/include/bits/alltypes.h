/* wasm-cxx-shim hand-rolled <bits/alltypes.h>.
 *
 * SHARED HEADER: this exact content is duplicated in both
 *   libc/include/bits/alltypes.h
 *   libm/include/bits/alltypes.h
 * — must stay byte-for-byte identical. See the matching note in
 * features.h for why.
 *
 * In musl, alltypes.h is GENERATED per-arch from alltypes.h.in via a
 * sed-substitution pass that fills in TYPEDEF macros. We hand-write it
 * here, covering ONLY the __NEED_* macros that headers we ship actually
 * set. Add new __NEED_* blocks here, not by importing musl's .h.in.
 *
 * On wasm32:
 *   * size_t == unsigned long  (clang's choice for wasm32)
 *   * ptrdiff_t == long
 *   * wchar_t == int           (clang default)
 *   * float_t/double_t == float/double  (since __FLT_EVAL_METHOD__ == 0)
 *
 * IMPORTANT: this file is intentionally NOT include-guarded. Musl
 * headers each set their own subset of __NEED_* macros and re-include
 * <bits/alltypes.h> — relying on the per-typedef __DEFINED_* interlock
 * to make repeat inclusion safe. An outer include guard would break
 * that pattern: a second inclusion (with a different __NEED_* set)
 * would short-circuit before emitting the new typedefs.
 */

/* size_t */
#if defined(__NEED_size_t) && !defined(__DEFINED_size_t)
typedef unsigned long size_t;
#define __DEFINED_size_t
#endif

/* ptrdiff_t */
#if defined(__NEED_ptrdiff_t) && !defined(__DEFINED_ptrdiff_t)
typedef long ptrdiff_t;
#define __DEFINED_ptrdiff_t
#endif

/* wchar_t */
#if defined(__NEED_wchar_t) && !defined(__DEFINED_wchar_t)
#  ifdef __cplusplus
   /* in C++, wchar_t is a real keyword/type; don't typedef */
#  else
typedef int wchar_t;
#  endif
#define __DEFINED_wchar_t
#endif

/* wint_t */
#if defined(__NEED_wint_t) && !defined(__DEFINED_wint_t)
typedef int wint_t;
#define __DEFINED_wint_t
#endif

/* mbstate_t — opaque, sized for any plausible state machine */
#if defined(__NEED_mbstate_t) && !defined(__DEFINED_mbstate_t)
typedef struct __mbstate_t {
    unsigned __opaque1;
    unsigned __opaque2;
} mbstate_t;
#define __DEFINED_mbstate_t
#endif

/* FILE — wasm32 has no real stdio, so this is an opaque type. Consumer
 * code that calls fopen/fprintf/etc. will get a link error from our
 * shim, which is the correct outcome. */
#if defined(__NEED_FILE) && !defined(__DEFINED_FILE)
typedef struct _IO_FILE FILE;
#define __DEFINED_FILE
#endif

#if defined(__NEED_struct__IO_FILE) && !defined(__DEFINED_struct__IO_FILE)
struct _IO_FILE { char __x; };
#define __DEFINED_struct__IO_FILE
#endif

/* va_list / __isoc_va_list */
#if defined(__NEED_va_list) && !defined(__DEFINED_va_list)
typedef __builtin_va_list va_list;
#define __DEFINED_va_list
#endif

#if defined(__NEED___isoc_va_list) && !defined(__DEFINED___isoc_va_list)
typedef __builtin_va_list __isoc_va_list;
#define __DEFINED___isoc_va_list
#endif

/* float_t / double_t — clang on wasm32 has __FLT_EVAL_METHOD__ == 0,
 * so they are simply float / double. */
#if defined(__NEED_float_t) && !defined(__DEFINED_float_t)
typedef float float_t;
#define __DEFINED_float_t
#endif

#if defined(__NEED_double_t) && !defined(__DEFINED_double_t)
typedef double double_t;
#define __DEFINED_double_t
#endif

/* max_align_t */
#if defined(__NEED_max_align_t) && !defined(__DEFINED_max_align_t)
typedef struct { long long __ll; long double __ld; } max_align_t;
#define __DEFINED_max_align_t
#endif

/* locale_t — opaque; we don't ship locale support, but some headers
 * mention the type. */
#if defined(__NEED_locale_t) && !defined(__DEFINED_locale_t)
typedef struct __locale_struct *locale_t;
#define __DEFINED_locale_t
#endif

/* wctype_t — opaque; same rationale */
#if defined(__NEED_wctype_t) && !defined(__DEFINED_wctype_t)
typedef unsigned long wctype_t;
#define __DEFINED_wctype_t
#endif

/* ssize_t / off_t — set by stdio.h under any non-strict feature mode */
#if defined(__NEED_ssize_t) && !defined(__DEFINED_ssize_t)
typedef long ssize_t;
#define __DEFINED_ssize_t
#endif

#if defined(__NEED_off_t) && !defined(__DEFINED_off_t)
typedef long long off_t;
#define __DEFINED_off_t
#endif
