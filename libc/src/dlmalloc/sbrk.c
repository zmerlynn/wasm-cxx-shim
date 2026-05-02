/* libc/src/dlmalloc/sbrk.c — synthetic sbrk over wasm memory.grow.
 *
 * Adapted from WebAssembly/wasi-libc/libc-bottom-half/sources/sbrk.c
 * (MIT). Wraps __builtin_wasm_memory_grow / __builtin_wasm_memory_size
 * so dlmalloc's MORECORE call resolves entirely in-wasm — no WASI
 * import, despite the function being named `sbrk`.
 *
 * Contract dlmalloc requires (per malloc.c):
 *   sbrk(0)        → current break (top of valid linear memory)
 *   sbrk(n>0)      → grow by n bytes; return old break
 *   sbrk(n<0)      → never called (we set MORECORE_CANNOT_TRIM=1)
 *   on OOM         → return (void*)-1
 *
 * dlmalloc rounds requests up to its granularity, which under our
 * cocktail equals the wasm page size, so increment is always a
 * multiple of PAGESIZE in practice. The trap on misuse is defensive.
 */

#include <stddef.h>
#include <stdint.h>

#define PAGESIZE 65536

void* sbrk(intptr_t increment) {
    if (increment == 0) {
        return (void*)(__builtin_wasm_memory_size(0) * PAGESIZE);
    }
    if (increment < 0 || (increment % PAGESIZE) != 0) {
        __builtin_trap();
    }
    uintptr_t old = __builtin_wasm_memory_grow(0, (uintptr_t)increment / PAGESIZE);
    if (old == SIZE_MAX) {
        return (void*)-1;
    }
    return (void*)(old * PAGESIZE);
}
