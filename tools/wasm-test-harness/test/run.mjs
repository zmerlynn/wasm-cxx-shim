// tools/wasm-test-harness/test/run.mjs — self-test runner for the harness.
//
// Validates the harness mechanism itself: load the self-test wasm,
// invoke wcs_run_tests(), then assert that the return value, log
// summary line, and a few specific log strings match what the
// self_test.c sources are designed to produce.
//
// Distinct from the generic ../run.mjs because that one passes-through
// the failure count as the runner exit status — and we *want* the
// self-test wasm to report failures (that's what it's testing).

import fs from 'node:fs';
import process from 'node:process';

if (process.argv.length < 3) {
    console.error('usage: node run.mjs <self-test-wasm>');
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
const logPtr   = exp.wcs_log_buffer();
const logSize  = exp.wcs_log_size();
const log      = new TextDecoder('utf-8').decode(
    new Uint8Array(exp.memory.buffer, logPtr, logSize));

// Print full log up front so debugging a regression is one log read.
process.stdout.write(log);

// ---- Expectations ---------------------------------------------------------
//
// MUST be kept in sync with self_test.cpp. The runner is intentionally
// strict on the test/pass/fail counts so adding or removing a test in
// self_test.cpp without updating these constants surfaces immediately,
// rather than silently shifting the harness-validates-itself baseline.
//
// `singleFailureTestsAtLeast` is a lower-bound check (use ">=") because
// the *number* of single-assertion-failure tests is incidental to what
// we're validating: that the harness records exactly 1 failure per FAIL
// line, and that REQUIRE-abandons-test really does abandon (the
// post-REQUIRE CHECK in RequireAbandonsTest must NOT fire).
const EXPECTED = {
    failingTests:              4,    // FailingCheck, FailingCheckEq,
                                     // MultipleFailingChecks, RequireAbandonsTest
    passingTests:              3,    // PassingCheck, PassingMultipleChecks, RequirePassing
    totalTests:                7,
    multipleFailureAssertions: 3,    // MultipleFailingChecks records 3 in one test
    singleFailureTestsAtLeast: 3,    // FailingCheck + FailingCheckEq + RequireAbandonsTest
};

// ---- Assertions -----------------------------------------------------------

let problems = 0;
const fail = (msg) => { console.error(`SELF-TEST FAIL: ${msg}`); ++problems; };

// wcs_run_tests returns the fail-count — strict equality.
if (failures !== EXPECTED.failingTests)
    fail(`wcs_run_tests() returned ${failures}, expected ${EXPECTED.failingTests}`);

// Summary line.
const summary = `wasm-test-harness: ${EXPECTED.passingTests} passed, ` +
                `${EXPECTED.failingTests} failed, ${EXPECTED.totalTests} total`;
if (!log.includes(summary)) fail(`log missing expected summary: '${summary}'`);

// MultipleFailingChecks: N assertion failures in one test.
const multiFail = `FAILED (${EXPECTED.multipleFailureAssertions} assertion failures)`;
if (!log.includes(multiFail))
    fail(`log missing '${multiFail}' (MultipleFailingChecks)`);

// REQUIRE-abandons-test: failing REQUIRE returns from the function, so
// the unreachable CHECK never fires; the test records exactly 1
// assertion failure. The "at least N" lower bound is deliberate (see
// the EXPECTED block comment) — multiple tests produce
// '(1 assertion failures)' and we don't care which.
const oneFail = '(1 assertion failures)';
const oneFailCount = log.split(oneFail).length - 1;
if (oneFailCount < EXPECTED.singleFailureTestsAtLeast)
    fail(`log shows '${oneFail}' ${oneFailCount} time(s); expected >= ` +
         `${EXPECTED.singleFailureTestsAtLeast}`);

// PassingMultipleChecks: 6 successive passing CHECKs — ok status.
if (!log.includes('WcsHarness.PassingMultipleChecks ... ok'))
    fail("log missing 'WcsHarness.PassingMultipleChecks ... ok'");

// Spot-check a failure log includes the expression text.
if (!log.includes('FAIL') || !log.includes('self_test.cpp'))
    fail("log missing FAIL line referencing self_test.cpp");

if (problems > 0) {
    console.error(`\nharness self-test: ${problems} assertion(s) failed`);
    process.exit(1);
}
console.log('\nharness self-test: all assertions passed ✓');
process.exit(0);
