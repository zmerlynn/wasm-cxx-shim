// tools/wasm-test-harness/run.mjs — Node runner for wasm-test-harness wasms.
//
// Loads a wasm produced by linking against wasm-cxx-shim-test-harness,
// invokes wcs_run_tests(), reads the log buffer, prints it, exits with
// the failure count.
//
// Usage: node run.mjs <wasm-file>

import fs from 'node:fs';
import process from 'node:process';

if (process.argv.length < 3) {
    console.error('usage: node run.mjs <wasm-file>');
    process.exit(2);
}

const bytes = fs.readFileSync(process.argv[2]);
const { instance } = await WebAssembly.instantiate(bytes, {});
const exp = instance.exports;

for (const required of ['wcs_run_tests', 'wcs_log_buffer', 'wcs_log_size', 'memory']) {
    if (!exp[required]) {
        console.error(`wasm is missing required export: ${required}`);
        console.error(`available exports: ${Object.keys(exp).join(', ')}`);
        process.exit(1);
    }
}

const failures = exp.wcs_run_tests();

// Read the log buffer from wasm linear memory.
const logPtr  = exp.wcs_log_buffer();
const logSize = exp.wcs_log_size();
const memory  = new Uint8Array(exp.memory.buffer, logPtr, logSize);
const log     = new TextDecoder('utf-8').decode(memory);

process.stdout.write(log);
process.exit(failures === 0 ? 0 : 1);
