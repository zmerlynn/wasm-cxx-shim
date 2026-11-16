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

// ---- Assertion plumbing -----------------------------------------------------
//
// GoogleTest assertions are expressions that accept a streamed message:
//     EXPECT_NEAR(a, b, eps) << "context " << i;
// so they cannot expand to the harness's do { } while (0) blocks. Same
// shape as GoogleTest's own: on failure, the message object is built,
// any `<<` operands bind to it, and assigning it to an AssertHelper
// records the failure through wcs_record_failure(). The streamed message
// itself is dropped; the harness only logs the failed expression. ASSERT_*
// puts `return` in front, which works because test bodies return void.
// The switch makes the macro safe as the body of an unbraced if/else.

namespace wcs_gtest_detail {

struct Message {
    template <class T>
    Message& operator<<(const T&) { return *this; }
};

struct AssertHelper {
    const char* file;
    int line;
    const char* expr;
    void operator=(const Message&) const { wcs_record_failure(file, line, expr); }
};

}  // namespace wcs_gtest_detail

#define WCS_GTEST_CHECK_(cond, text)                                          \
    switch (0) case 0: default:                                               \
    if (cond) ; else                                                          \
        ::wcs_gtest_detail::AssertHelper{__FILE__, __LINE__, text} =          \
            ::wcs_gtest_detail::Message()

#define WCS_GTEST_REQUIRE_(cond, text)                                        \
    switch (0) case 0: default:                                               \
    if (cond) ; else                                                          \
        return ::wcs_gtest_detail::AssertHelper{__FILE__, __LINE__, text} =   \
            ::wcs_gtest_detail::Message()

// ---- Boolean / equality / relational ---------------------------------------

#define EXPECT_TRUE(c)        WCS_GTEST_CHECK_((c), #c)
#define EXPECT_FALSE(c)       WCS_GTEST_CHECK_(!(c), "!(" #c ")")
#define EXPECT_EQ(a, b)       WCS_GTEST_CHECK_((a) == (b), #a " == " #b)
#define EXPECT_NE(a, b)       WCS_GTEST_CHECK_((a) != (b), #a " != " #b)
#define EXPECT_LT(a, b)       WCS_GTEST_CHECK_((a) <  (b), #a " < " #b)
#define EXPECT_LE(a, b)       WCS_GTEST_CHECK_((a) <= (b), #a " <= " #b)
#define EXPECT_GT(a, b)       WCS_GTEST_CHECK_((a) >  (b), #a " > " #b)
#define EXPECT_GE(a, b)       WCS_GTEST_CHECK_((a) >= (b), #a " >= " #b)

#define ASSERT_TRUE(c)        WCS_GTEST_REQUIRE_((c), #c)
#define ASSERT_FALSE(c)       WCS_GTEST_REQUIRE_(!(c), "!(" #c ")")
#define ASSERT_EQ(a, b)       WCS_GTEST_REQUIRE_((a) == (b), #a " == " #b)
#define ASSERT_NE(a, b)       WCS_GTEST_REQUIRE_((a) != (b), #a " != " #b)
#define ASSERT_LT(a, b)       WCS_GTEST_REQUIRE_((a) <  (b), #a " < " #b)
#define ASSERT_LE(a, b)       WCS_GTEST_REQUIRE_((a) <= (b), #a " <= " #b)
#define ASSERT_GT(a, b)       WCS_GTEST_REQUIRE_((a) >  (b), #a " > " #b)
#define ASSERT_GE(a, b)       WCS_GTEST_REQUIRE_((a) >= (b), #a " >= " #b)

// ---- Floating-point comparisons --------------------------------------------

// EXPECT_FLOAT_EQ / EXPECT_DOUBLE_EQ use 4-ULP comparison (matching
// real GoogleTest). EXPECT_NEAR is straight `|a-b| < eps`, as in the
// harness's WCS_CHECK_NEAR.
#define WCS_GTEST_NEAR_(a, b, eps) (((a) - (b) > -(eps)) && ((a) - (b) < (eps)))

#define EXPECT_FLOAT_EQ(a, b)   WCS_GTEST_CHECK_(::wcs::test::float_eq_4ulp((a), (b)), #a " ~= " #b)
#define EXPECT_DOUBLE_EQ(a, b)  WCS_GTEST_CHECK_(::wcs::test::double_eq_4ulp((a), (b)), #a " ~= " #b)
#define EXPECT_NEAR(a, b, eps)  WCS_GTEST_CHECK_(WCS_GTEST_NEAR_(a, b, eps), #a " ~= " #b " within " #eps)

#define ASSERT_FLOAT_EQ(a, b)   WCS_GTEST_REQUIRE_(::wcs::test::float_eq_4ulp((a), (b)), #a " ~= " #b)
#define ASSERT_DOUBLE_EQ(a, b)  WCS_GTEST_REQUIRE_(::wcs::test::double_eq_4ulp((a), (b)), #a " ~= " #b)
#define ASSERT_NEAR(a, b, eps)  WCS_GTEST_REQUIRE_(WCS_GTEST_NEAR_(a, b, eps), #a " ~= " #b " within " #eps)

// ---- SCOPED_TRACE(message) --------------------------------------------------
//
// Real GoogleTest attaches the message to any failure inside the scope.
// The harness has no failure-context stack, so this is a no-op: failures
// still report, just without the extra context. The message is not
// evaluated.

#define SCOPED_TRACE(message) static_cast<void>(0)

// ---- std::cerr / std::endl ---------------------------------------------------
//
// Real gtest.h includes <ostream>, so test sources can use std::cerr
// without including <iostream> themselves (manifold's do). With libc++'s
// localization off, as the shim builds it, <iostream> declares neither.
// Provide a discarding stand-in so such sources compile; anything written
// to it is dropped. Only defined when libc++ itself has no iostreams, so
// it cannot collide with the real ones.

#include <__config>

#if defined(_LIBCPP_HAS_LOCALIZATION) && !_LIBCPP_HAS_LOCALIZATION
namespace wcs_gtest_detail {
struct DiscardStream {
    template <class T>
    DiscardStream& operator<<(const T&) { return *this; }
    DiscardStream& operator<<(DiscardStream& (*manip)(DiscardStream&)) { return manip(*this); }
};
inline DiscardStream discard_stream;
}  // namespace wcs_gtest_detail

namespace std {
inline ::wcs_gtest_detail::DiscardStream& cerr = ::wcs_gtest_detail::discard_stream;
inline ::wcs_gtest_detail::DiscardStream& cout = ::wcs_gtest_detail::discard_stream;
inline ::wcs_gtest_detail::DiscardStream& endl(::wcs_gtest_detail::DiscardStream& s) { return s; }
}  // namespace std
#endif

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
