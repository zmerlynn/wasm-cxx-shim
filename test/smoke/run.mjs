// test/smoke/run.mjs — smoke test runner.
//
// Loads the compiled smoke.wasm, calls run(), asserts the return
// value, and exits 0 on success or non-zero on mismatch.
//
// Usage: node run.mjs <path-to-smoke.wasm>

import fs from 'node:fs';
import process from 'node:process';

const EXPECTED = 109; // sum(1..5) + sum(squares 1..5) + 7 + sqrt(1024) + 0
                      //     = 15 + 55 + 7 + 32 + 0 = 109

if (process.argv.length < 3) {
    console.error('usage: node run.mjs <path-to-smoke.wasm>');
    process.exit(2);
}

const wasmPath = process.argv[2];
const bytes = fs.readFileSync(wasmPath);
const { instance } = await WebAssembly.instantiate(bytes, {});

if (typeof instance.exports.run !== 'function') {
    console.error(`smoke: wasm has no exported 'run' function`);
    console.error(`exports: ${Object.keys(instance.exports).join(', ')}`);
    process.exit(1);
}

const got = instance.exports.run();
if (got !== EXPECTED) {
    console.error(`smoke: run() returned ${got}, expected ${EXPECTED}`);
    process.exit(1);
}

console.log(`smoke: run() = ${got} (expected ${EXPECTED}) ✓`);
