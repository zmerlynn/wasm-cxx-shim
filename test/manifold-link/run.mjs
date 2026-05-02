// test/manifold-link/run.mjs — manifold-link probe runner.
//
// Loads the probe wasm, calls probe_run(), asserts a positive
// triangle count. The exact value isn't load-bearing — what matters
// is that manifold's CSG kernel actually executes on the shim
// (boolean union of two cubes through manifold_cube → translate →
// boolean(ADD) → num_tri). A zero or negative return means
// something's broken (allocation failure, dead-stripped function
// call, etc.).

import fs from 'node:fs';
import process from 'node:process';

if (process.argv.length < 3) {
    console.error('usage: node run.mjs <path-to-manifold-link-probe.wasm>');
    process.exit(2);
}

const bytes = fs.readFileSync(process.argv[2]);
const { instance } = await WebAssembly.instantiate(bytes, {});

if (typeof instance.exports.probe_run !== 'function') {
    console.error(`manifold-link: wasm has no exported probe_run`);
    process.exit(1);
}

const got = instance.exports.probe_run();
if (!Number.isInteger(got) || got <= 0) {
    console.error(`manifold-link: probe_run returned ${got}; expected positive integer (triangle count)`);
    process.exit(1);
}

console.log(`manifold-link: probe_run() = ${got} (triangle count, > 0) ✓`);
