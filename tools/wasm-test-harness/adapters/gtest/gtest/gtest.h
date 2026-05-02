// tools/wasm-test-harness/adapters/gtest/gtest/gtest.h
//
// Adapter that lets GoogleTest-using consumers compile against the
// wasm-test-harness without modifying their test sources. Maps the
// GoogleTest macro subset most C++ projects actually use to our
// WCS_* primitives in <wcs-test.h>.
//
// To use: arrange your build so that this directory comes BEFORE the
// real GoogleTest include path on `-isystem`. The consumer's
// `#include "gtest/gtest.h"` will resolve here, and `TEST(...)`,
// `EXPECT_EQ(...)` etc. expand to harness calls.
//
// ASSERT_* vs EXPECT_* semantics: real GoogleTest's ASSERT_* macros
// use a non-local return to abandon the current test on failure; this
// adapter implements that with `WCS_REQUIRE_*` which calls `return`
// from the test function. Tests that wrap ASSERT_* in helper functions
// won't propagate the abandon-test semantic across the helper boundary
// — the helper returns, the test continues. For most tests this is
// fine; for tests that genuinely depend on early-exit semantics,
// rewrite the helper or re-check the condition in the caller.
//
// Things this adapter does NOT support (intentionally, for the first
// cut):
//   - Test fixtures (TEST_F, ::testing::Test base class)
//   - Parameterized tests (TEST_P)
//   - Custom matchers (EXPECT_THAT, ::testing::Matcher)
//   - Death tests (EXPECT_DEATH)
//   - GMock
//
// If a consumer's test uses these, either rewrite the test or extend
// the adapter.

#ifndef _WCS_GTEST_GTEST_H
#define _WCS_GTEST_GTEST_H

#include <wcs-test.h>

// ---- TEST(SuiteName, CaseName) ---------------------------------------------

#define TEST(SUITE, NAME) WCS_TEST(SUITE, NAME)

// ---- Boolean / equality / relational ---------------------------------------

#define EXPECT_TRUE(c)        WCS_CHECK(c)
#define EXPECT_FALSE(c)       WCS_CHECK(!(c))
#define EXPECT_EQ(a, b)       WCS_CHECK_EQ(a, b)
#define EXPECT_NE(a, b)       WCS_CHECK_NE(a, b)
#define EXPECT_LT(a, b)       WCS_CHECK_LT(a, b)
#define EXPECT_LE(a, b)       WCS_CHECK_LE(a, b)
#define EXPECT_GT(a, b)       WCS_CHECK_GT(a, b)
#define EXPECT_GE(a, b)       WCS_CHECK_GE(a, b)

#define ASSERT_TRUE(c)        WCS_REQUIRE(c)
#define ASSERT_FALSE(c)       WCS_REQUIRE(!(c))
#define ASSERT_EQ(a, b)       WCS_REQUIRE_EQ(a, b)
#define ASSERT_NE(a, b)       WCS_REQUIRE_NE(a, b)
#define ASSERT_LT(a, b)       WCS_REQUIRE_LT(a, b)
#define ASSERT_LE(a, b)       WCS_REQUIRE_LE(a, b)
#define ASSERT_GT(a, b)       WCS_REQUIRE_GT(a, b)
#define ASSERT_GE(a, b)       WCS_REQUIRE_GE(a, b)

// ---- Floating-point comparisons --------------------------------------------

// EXPECT_FLOAT_EQ / EXPECT_DOUBLE_EQ use 4-ULP comparison (matching
// real GoogleTest). EXPECT_NEAR is straight `|a-b| < eps`.
#define EXPECT_FLOAT_EQ(a, b)   WCS_CHECK_FLOAT_EQ(a, b)
#define EXPECT_DOUBLE_EQ(a, b)  WCS_CHECK_DOUBLE_EQ(a, b)
#define EXPECT_NEAR(a, b, eps)  WCS_CHECK_NEAR(a, b, eps)

#define ASSERT_FLOAT_EQ(a, b)   WCS_REQUIRE_FLOAT_EQ(a, b)
#define ASSERT_DOUBLE_EQ(a, b)  WCS_REQUIRE_DOUBLE_EQ(a, b)
#define ASSERT_NEAR(a, b, eps)  WCS_REQUIRE_NEAR(a, b, eps)

// ---- Test main shims --------------------------------------------------------

namespace testing {

// no-op; tests are registered via static initializer
inline void InitGoogleTest(int* /*argc*/, char** /*argv*/) {}
inline void InitGoogleTest() {}

}  // namespace testing

// RUN_ALL_TESTS() is consumed by the consumer's main(). We don't
// actually use it — our test runner is wcs_run_tests() invoked from
// the wasm export. But some test_main.cpp files reference it, so
// provide a working symbol.
inline int RUN_ALL_TESTS() { return wcs_run_tests(); }

#endif // _WCS_GTEST_GTEST_H
