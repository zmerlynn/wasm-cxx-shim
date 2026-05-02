// tools/wasm-test-harness/test/self_test.cpp
//
// Self-test for the wasm-test-harness primitives: register a mix of
// deliberately-passing and deliberately-failing tests, then let the
// custom runner (run.mjs in this dir) assert that the harness counts
// and log output match expectations.
//
// Compiled as C++ (matches the consumer test-source pattern; WCS_TEST's
// static-initializer registration relies on C++ dynamic initialization,
// since pure-C file-scope initializers must be constant expressions).
// Includes only <wcs-test.h> — no libc++ headers — so the build stays
// light (harness + libc only, no libcxx involvement).

#include <wcs-test.h>

// ---- Passing tests ---------------------------------------------------------

WCS_TEST(WcsHarness, PassingCheck) {
    WCS_CHECK(1 + 1 == 2);
}

WCS_TEST(WcsHarness, PassingMultipleChecks) {
    WCS_CHECK_EQ(1, 1);
    WCS_CHECK_NE(1, 2);
    WCS_CHECK_LT(1, 2);
    WCS_CHECK_LE(1, 1);
    WCS_CHECK_GT(2, 1);
    WCS_CHECK_GE(2, 2);
}

WCS_TEST(WcsHarness, RequirePassing) {
    WCS_REQUIRE(1);
    WCS_CHECK(1);
}

// ---- Failing tests ---------------------------------------------------------

WCS_TEST(WcsHarness, FailingCheck) {
    WCS_CHECK(0);
}

WCS_TEST(WcsHarness, FailingCheckEq) {
    WCS_CHECK_EQ(2, 3);
}

// Three independent CHECK failures in one test — should still count as
// exactly 1 *test* failed (with 3 *assertion* failures in the log).
WCS_TEST(WcsHarness, MultipleFailingChecks) {
    WCS_CHECK(0);
    WCS_CHECK_EQ(1, 2);
    WCS_CHECK_LT(5, 1);
}

// REQUIRE-style: a failing REQUIRE returns from the test function, so
// the subsequent CHECK does NOT fire. Validates the abandon-on-failure
// semantics. Should record exactly 1 assertion failure.
WCS_TEST(WcsHarness, RequireAbandonsTest) {
    WCS_REQUIRE(0);
    WCS_CHECK(0);  // unreachable; if it fires, the count is wrong
}
