/* Vendored verbatim from wasi-libc's musl tree.
 * Source: https://github.com/WebAssembly/wasi-libc/blob/main/libc-top-half/musl/arch/wasm32/atomic_arch.h
 * License: MIT (see LICENSES/LICENSE-musl).
 */
#define a_cas(p, t, s) (__sync_val_compare_and_swap((p), (t), (s)))
#define a_crash() (__builtin_trap())
#define a_clz_32 __builtin_clz
#define a_clz_64 __builtin_clzll
#define a_ctz_32 __builtin_ctz
#define a_ctz_64 __builtin_ctzll
