/* libc/include/endian.h — minimal <endian.h> for wasm32.
 *
 * Wasm is little-endian per WebAssembly Core Spec §2.1.4 ("Memory").
 * musl's mem and math sources include this header for the
 * __BYTE_ORDER / __LITTLE_ENDIAN / __BIG_ENDIAN macros.
 */
#ifndef _WASM_CXX_SHIM_ENDIAN_H
#define _WASM_CXX_SHIM_ENDIAN_H

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321
#define __PDP_ENDIAN    3412
#define __BYTE_ORDER    __LITTLE_ENDIAN

#endif
