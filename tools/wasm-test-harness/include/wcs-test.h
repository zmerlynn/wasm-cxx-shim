// tools/wasm-test-harness/include/wcs-test.h
//
// Minimal test harness primitives for running consumer test suites on
// wasm32-unknown-unknown via the shim. Not a complete test framework —
// just enough to be a target for translation adapters (currently:
// adapters/gtest/ for GoogleTest-using consumers).
//
// Usage at the test-source level:
//
//   #include <wcs-test.h>
//
//   WCS_TEST(MySuite, MyCase) {
//       int x = some_function(42);
//       WCS_CHECK_EQ(x, 42);
//   }
//
// At link time: link against wcs-test-harness.a and call wcs_run_tests()
// from your wasm entry. The harness registry, log buffer, and runner all
// live in the .a — no separate `main()` is required.
//
// The harness deliberately avoids:
//   - <iostream>, <sstream> (libc++ gates them behind localization)
//   - threads, <condition_variable>
//   - <fstream>, filesystem
//   - exceptions
//
// Output is a fixed-size byte buffer the runner appends to via
// wcs_log_printf; the host (Node) reads it after run.

#ifndef _WCS_TEST_H
#define _WCS_TEST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Registry ---------------------------------------------------------------

typedef void (*wcs_test_fn)(void);

// Register a test function. Returns a dummy int so it can run from a
// global initializer. Idempotent within a process; the test list is
// stable across calls.
int wcs_register_test(const char* suite, const char* name, wcs_test_fn fn);

// Iteration (used by the runner; consumers normally don't touch).
size_t wcs_test_count(void);
const char* wcs_test_suite(size_t i);
const char* wcs_test_name(size_t i);
wcs_test_fn wcs_test_fn_at(size_t i);

// ---- Per-test result tracking -----------------------------------------------

// The current test's failure count. wcs_check_* increment this; a
// non-zero value at end-of-test marks the test as failed.
int  wcs_current_failures(void);
void wcs_record_failure(const char* file, int line, const char* expr);

// ---- Runner -----------------------------------------------------------------

// Runs all registered tests. Returns the number of failed tests.
// Intended to be the single wasm export from a test wasm.
int wcs_run_tests(void);

// ---- Log buffer (host-readable) ---------------------------------------------

// Appends a printf-style message to the internal log buffer. Truncates
// silently when the buffer fills. Returns bytes written.
int wcs_log_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Pointer to the log buffer + how many bytes have been written. The host
// reads these via wasm exports after wcs_run_tests() returns.
const char* wcs_log_buffer(void);
size_t      wcs_log_size(void);

#ifdef __cplusplus
}  // extern "C"
#endif

// ---- Test-source-facing macros ---------------------------------------------

#define _WCS_CONCAT2(a, b) a##b
#define _WCS_CONCAT(a, b)  _WCS_CONCAT2(a, b)

// WCS_TEST(SuiteName, CaseName) { ... body ... }
//
// Defines a void-returning test function and registers it via a static
// initializer. The body runs in the order tests are registered (which
// for in-tree consumers is link order; not deterministic across TUs).
#define WCS_TEST(SUITE, NAME)                                               \
    static void _WCS_CONCAT(_wcs_test_, _WCS_CONCAT(SUITE, _##NAME))(void); \
    static int  _WCS_CONCAT(_wcs_reg_,  _WCS_CONCAT(SUITE, _##NAME))       \
        = wcs_register_test(#SUITE, #NAME,                                  \
                            &_WCS_CONCAT(_wcs_test_, _WCS_CONCAT(SUITE, _##NAME))); \
    static void _WCS_CONCAT(_wcs_test_, _WCS_CONCAT(SUITE, _##NAME))(void)

// Assertion macros. WCS_CHECK_* records a failure and continues
// (EXPECT-style); WCS_REQUIRE_* records and returns from the test
// function (ASSERT-style).
#define WCS_CHECK(expr)                                                     \
    do {                                                                    \
        if (!(expr)) wcs_record_failure(__FILE__, __LINE__, #expr);         \
    } while (0)

#define WCS_REQUIRE(expr)                                                   \
    do {                                                                    \
        if (!(expr)) { wcs_record_failure(__FILE__, __LINE__, #expr);       \
                       return; }                                            \
    } while (0)

// Equality/relational variants. Implemented in terms of the boolean
// form so consumers that pass complex expressions still get a useful
// failure message via the stringified `#expr`.
#define WCS_CHECK_EQ(a, b)   WCS_CHECK((a) == (b))
#define WCS_CHECK_NE(a, b)   WCS_CHECK((a) != (b))
#define WCS_CHECK_LT(a, b)   WCS_CHECK((a) <  (b))
#define WCS_CHECK_LE(a, b)   WCS_CHECK((a) <= (b))
#define WCS_CHECK_GT(a, b)   WCS_CHECK((a) >  (b))
#define WCS_CHECK_GE(a, b)   WCS_CHECK((a) >= (b))
#define WCS_REQUIRE_EQ(a, b) WCS_REQUIRE((a) == (b))
#define WCS_REQUIRE_NE(a, b) WCS_REQUIRE((a) != (b))
#define WCS_REQUIRE_LT(a, b) WCS_REQUIRE((a) <  (b))
#define WCS_REQUIRE_LE(a, b) WCS_REQUIRE((a) <= (b))
#define WCS_REQUIRE_GT(a, b) WCS_REQUIRE((a) >  (b))
#define WCS_REQUIRE_GE(a, b) WCS_REQUIRE((a) >= (b))

// Floating-point near-equality. Uses a fabs comparison; the threshold
// is chosen by the caller (no automatic ULP-based reasoning).
#define WCS_CHECK_NEAR(a, b, eps)                                           \
    WCS_CHECK(((a) - (b) > -(eps)) && ((a) - (b) < (eps)))
#define WCS_REQUIRE_NEAR(a, b, eps)                                         \
    WCS_REQUIRE(((a) - (b) > -(eps)) && ((a) - (b) < (eps)))

// 4-ULP equality (matches GoogleTest's EXPECT_FLOAT_EQ / EXPECT_DOUBLE_EQ
// semantics). Implemented via integer comparison of the bit
// representations — same approach as GoogleTest's internal FloatPoint
// helper. Inline so each call site evaluates `a` / `b` exactly once.
#ifdef __cplusplus
namespace wcs { namespace test {

inline bool float_eq_4ulp(float a, float b) {
    if (a == b) return true;
    if (!(a == a) || !(b == b)) return false;  // NaN
    int32_t ai, bi;
    __builtin_memcpy(&ai, &a, 4);
    __builtin_memcpy(&bi, &b, 4);
    if ((ai < 0) != (bi < 0)) return false;    // different signs
    int32_t diff = ai > bi ? ai - bi : bi - ai;
    return diff <= 4;
}

inline bool double_eq_4ulp(double a, double b) {
    if (a == b) return true;
    if (!(a == a) || !(b == b)) return false;
    int64_t ai, bi;
    __builtin_memcpy(&ai, &a, 8);
    __builtin_memcpy(&bi, &b, 8);
    if ((ai < 0) != (bi < 0)) return false;
    int64_t diff = ai > bi ? ai - bi : bi - ai;
    return diff <= 4;
}

}}
#endif  // __cplusplus

#define WCS_CHECK_FLOAT_EQ(a, b)    WCS_CHECK(::wcs::test::float_eq_4ulp((a), (b)))
#define WCS_REQUIRE_FLOAT_EQ(a, b)  WCS_REQUIRE(::wcs::test::float_eq_4ulp((a), (b)))
#define WCS_CHECK_DOUBLE_EQ(a, b)   WCS_CHECK(::wcs::test::double_eq_4ulp((a), (b)))
#define WCS_REQUIRE_DOUBLE_EQ(a, b) WCS_REQUIRE(::wcs::test::double_eq_4ulp((a), (b)))

#endif // _WCS_TEST_H
