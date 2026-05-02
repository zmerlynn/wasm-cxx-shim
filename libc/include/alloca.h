/* libc/include/alloca.h — alloca() declaration.
 *
 * Tiny shim: clang has __builtin_alloca which is the actual
 * implementation; the alloca() call site lowers to it via the
 * declaration here. No library symbol needed.
 */
#ifndef _WASM_CXX_SHIM_ALLOCA_H
#define _WASM_CXX_SHIM_ALLOCA_H

#include <features.h>

#define __NEED_size_t
#include <bits/alltypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define alloca __builtin_alloca

#ifdef __cplusplus
}
#endif

#endif
