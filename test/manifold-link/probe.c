/* test/manifold-link/probe.c
 *
 * Tiny consumer of manifold's C API. Exercises a few entry points so
 * the linker pulls in enough of manifold's library to surface missing
 * shim symbols. The actual numeric output isn't load-bearing — what
 * matters is that compile + link both succeed against our libc/libm/
 * libcxx and the resulting .wasm has zero unexpected imports.
 *
 * API pattern (per manifold v3.4.1's bindings/c/include/manifold/manifoldc.h):
 *   ManifoldManifold *m = manifold_alloc_manifold();   // allocates raw memory
 *   m = manifold_cube(m, 1.0, 1.0, 1.0, 1);            // placement-news into m
 *   ...                                                // use m
 *   manifold_delete_manifold(m);                        // dtor + free
 */

#include <manifold/manifoldc.h>

extern int probe_run(void) {
    /* Cube → triangle count. Exercises construction + mesh export. */
    ManifoldManifold *cube_mem = manifold_alloc_manifold();
    ManifoldManifold *cube = manifold_cube(cube_mem, 1.0, 1.0, 1.0, 1);

    /* Boolean union with a translated copy of itself. Exercises the
     * CSG kernel, the BVH, and the symbolic-perturbation paths. */
    ManifoldManifold *cube2_mem = manifold_alloc_manifold();
    ManifoldManifold *cube2 = manifold_translate(cube2_mem, cube, 0.5, 0.5, 0.5);

    ManifoldManifold *u_mem = manifold_alloc_manifold();
    ManifoldManifold *u = manifold_boolean(u_mem, cube, cube2, MANIFOLD_ADD);

    int n_tris = (int)manifold_num_tri(u);

    manifold_delete_manifold(u);
    manifold_delete_manifold(cube2);
    manifold_delete_manifold(cube);

    return n_tris;
}
