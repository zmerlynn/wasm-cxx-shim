/* libc/src/dlmalloc/dlmalloc.c — wasm-cxx-shim wrapper around dlmalloc.
 *
 * Adapted from WebAssembly/wasi-libc/dlmalloc/src/dlmalloc.c (the
 * upstream WASI SDK's allocator wrapper, MIT/Apache-2.0). Our changes:
 *   - drop <malloc.h>/<errno.h>/musl alias machinery (wasi-libc-specific)
 *   - inline ENOMEM/EINVAL values rather than externing them
 *   - drop posix_memalign/malloc_usable_size/__libc_* (add by demand)
 *   - hardcode wasm32 page size (65536) so dlmalloc skips sysconf()
 *
 * The dlmalloc engine itself is in malloc.c, which is upstream Doug Lea
 * v2.8.6 verbatim (CC0). The cocktail of #defines below configures it
 * for freestanding wasm32-unknown-unknown.
 *
 * Cocktail rationale lives in CLAUDE.md. Don't change a single value
 * without updating that doc.
 */

#include <stddef.h>  /* size_t */
#include <stdint.h>  /* intptr_t, SIZE_MAX */

/* === dlmalloc cocktail === */

/* No mmap on wasm. */
#define HAVE_MMAP            0
/* Wasm linear memory cannot shrink. */
#define MORECORE_CANNOT_TRIM 1
/* Trap inline rather than calling abort(); avoids a libc dependency. */
#define ABORT                __builtin_unreachable()

/* Strip header dependencies that don't exist on freestanding wasm. */
#define LACKS_TIME_H         1
#define LACKS_ERRNO_H        1
#define LACKS_STDLIB_H       1
#define LACKS_STRING_H       1   /* we declare memset/memcpy below */
#define LACKS_UNISTD_H       1   /* malloc.c declares sbrk itself */
#define LACKS_SCHED_H        1
#define LACKS_SYS_PARAM_H    1
#define LACKS_SYS_TYPES_H    1   /* size_t comes from <stddef.h> */
#define LACKS_FCNTL_H        1

/* Drop diagnostic / stats code we don't need. */
#define NO_MALLINFO          1
#define NO_MALLOC_STATS      1

/* Align allocations to 16 bytes — covers std::align_val_t up to 16,
 * which is the most common SIMD/struct alignment request. Larger
 * alignment requests dispatch through dlmemalign(). */
#define MALLOC_ALIGNMENT     16

/* Keep dlmalloc's internals named dl*; we re-export the public C names
 * at the bottom of this file. Two reasons:
 *   - precise control over what symbols ship in the .a
 *   - prevent compilers from optimizing on the assumption that any
 *     function named "malloc" is THE malloc with its ABI guarantees */
#define USE_DL_PREFIX        1
#define DLMALLOC_EXPORT      static inline

/* Wasm32 page size (Wasm spec §2.1.4). Hardcoding skips dlmalloc's
 * sysconf(_SC_PAGESIZE) chain entirely. */
#define malloc_getpagesize   65536

/* errno values matching musl. dlmalloc only uses these to implement
 * MALLOC_FAILURE_ACTION (errno = ENOMEM); we override that to a no-op
 * just below, so these definitions exist only to satisfy the few
 * preprocessor sites that test #if ENOMEM == ... etc. */
#define ENOMEM 12
#define EINVAL 22

/* Make the failure action a no-op. Without this, malloc.c's default
 * MALLOC_FAILURE_ACTION expands to `errno = ENOMEM;` which references
 * a global we don't provide. */
#define MALLOC_FAILURE_ACTION  /* no-op */

/* mem* declarations malloc.c uses. Definitions live in libc/src/musl/. */
extern void* memset(void* dest, int c, size_t n);
extern void* memcpy(void* __restrict dest, const void* __restrict src, size_t n);

/* Internal-only; not re-exported. */
static size_t dlmalloc_usable_size(void*);

#include "malloc.c"

/* === Public allocator surface ===
 *
 * Each name added here is a permanent commitment — consumers will link
 * against it. Keep this list minimal; grow by demand. */

void* malloc(size_t size)                   { return dlmalloc(size); }
void  free(void* ptr)                       { dlfree(ptr); }
void* calloc(size_t n, size_t size)         { return dlcalloc(n, size); }
void* realloc(void* ptr, size_t size)       { return dlrealloc(ptr, size); }
void* aligned_alloc(size_t a, size_t size)  { return dlmemalign(a, size); }
