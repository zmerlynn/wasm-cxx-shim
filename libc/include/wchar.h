/* libc/include/wchar.h — minimal <wchar.h> for wasm-cxx-shim.
 *
 * Provides mbstate_t (used by libc++'s <__string/char_traits.h> for
 * `fpos<mbstate_t>` even when wide-char support is disabled at the
 * libc++ level). Plus the type plumbing musl-style headers expect.
 *
 * Function declarations are intentionally absent — wide-char functions
 * are out of scope. Consumers calling wcscpy/wmemcpy/etc. will get a
 * link error from this shim, which is the correct outcome.
 */
#ifndef _WASM_CXX_SHIM_WCHAR_H
#define _WASM_CXX_SHIM_WCHAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_FILE
#define __NEED_size_t
#define __NEED_wchar_t
#define __NEED_wint_t
#define __NEED_mbstate_t
#define __NEED_va_list

#include <bits/alltypes.h>

#define WEOF 0xffffffffU

#define WCHAR_MIN  (-1 - (wchar_t)0x7fffffff)
#define WCHAR_MAX  ((wchar_t)0x7fffffff)

#ifndef NULL
#  define NULL ((void*)0)
#endif

#ifdef __cplusplus
}
#endif

#endif
