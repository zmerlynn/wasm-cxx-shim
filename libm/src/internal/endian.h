/* libm/src/internal/endian.h — minimal <endian.h> for wasm32 (libm-private).
 *
 * Wasm is little-endian per WebAssembly Core Spec §2.1.4 ("Memory").
 * musl's libm.h pulls in <endian.h> for __BYTE_ORDER, used by the
 * long-double dispatch — which on wasm32 (LDBL_MANT_DIG == 53) hits the
 * first branch and never actually consults __BYTE_ORDER, but the
 * include must still resolve.
 *
 * Duplicated from libc/include/endian.h on purpose: libm and libc are
 * independently buildable per design, so libm shouldn't pull a header
 * from libc. Same content, separate copies.
 */
#ifndef _WASM_CXX_SHIM_LIBM_ENDIAN_H
#define _WASM_CXX_SHIM_LIBM_ENDIAN_H

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321
#define __PDP_ENDIAN    3412
#define __BYTE_ORDER    __LITTLE_ENDIAN

#endif
