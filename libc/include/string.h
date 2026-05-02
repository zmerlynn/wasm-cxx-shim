/* libc/include/string.h — minimal <string.h> for wasm-cxx-shim's libc.
 *
 * Provides only the mem* family that this libc actually implements.
 * Full <string.h> (str* family, locale-aware variants, etc.) is out of
 * scope; consumers needing those should bring a separate libc.
 */
#ifndef _WASM_CXX_SHIM_STRING_H
#define _WASM_CXX_SHIM_STRING_H

#include <stddef.h>  /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

void*  memcpy(void* __restrict dest, const void* __restrict src, size_t n);
void*  memmove(void* dest, const void* src, size_t n);
void*  memset(void* dest, int c, size_t n);
int    memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);

#ifdef __cplusplus
}
#endif

#endif
