/* test/manifold-link/probe.c
 *
 * Tiny consumer of manifold's C API. Exercises a few entry points so
 * the linker pulls in enough of manifold's library to surface missing
 * shim symbols. The actual numeric output isn't load-bearing — what
 * matters is that compile + link both succeed against our libc/libm/
 * libcxx and the resulting .wasm has zero unexpected imports.
 *
 * API pattern (manifold master, pre-4.0, after the C API stopped taking
 * caller-allocated memory):
 *   ManifoldManifold *m = manifold_cube(1.0, 1.0, 1.0, 1); // returns a new handle
 *   ...                                                    // use m
 *   manifold_delete_manifold(m);                            // dtor + free
 *
 * manifold 3.x took a `void* mem` first argument from manifold_alloc_*()
 * instead. Pinning an older MANIFOLD_GIT_TAG needs the older probe.
 */

#include <manifold/manifoldc.h>

extern int probe_run(void) {
    /* Cube → triangle count. Exercises construction + mesh export. */
    ManifoldManifold *cube = manifold_cube(1.0, 1.0, 1.0, 1);

    /* Boolean union with a translated copy of itself. Exercises the
     * CSG kernel, the BVH, and the symbolic-perturbation paths. */
    ManifoldManifold *cube2 = manifold_translate(cube, 0.5, 0.5, 0.5);

    ManifoldManifold *u = manifold_boolean(cube, cube2, MANIFOLD_ADD);

    int n_tris = (int)manifold_num_tri(u);

    manifold_delete_manifold(u);
    manifold_delete_manifold(cube2);
    manifold_delete_manifold(cube);

    return n_tris;
}
