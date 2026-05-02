/* test/consumer/external/consumer_probe.c
 *
 * Tiny C source that exercises libc + libm public surface through the
 * find_package'd targets. Compiled with the consumer's own (installed)
 * wasm-cxx-shim toolchain — so the headers come from the install
 * prefix, the archives come from the install prefix, and the build
 * tests the full round-trip.
 */

#include <stdlib.h>   /* malloc, free — from wasm-cxx-shim::libc */
#include <string.h>   /* memset      — from wasm-cxx-shim::libc */
#include <math.h>     /* sqrt        — from wasm-cxx-shim::libm */

int probe_run(int n, double x) {
    void *p = malloc((unsigned)n);
    if (!p) return -1;
    memset(p, 0xAB, (unsigned)n);
    free(p);
    return (int)sqrt(x);
}
