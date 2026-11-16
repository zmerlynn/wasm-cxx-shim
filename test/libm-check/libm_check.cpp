// test/libm-check/libm_check.cpp
//
// Correctness probes for the libm functions the shim ships. Not a
// full IEEE-precision conformance test — this is "sin(0)=0,
// sqrt(2)≈1.4142..." sanity. Catches regressions where a libm
// vendor bump or a compile-flag change alters a function's behavior.
//
// Tolerances are fixed-eps (1e-12 for tight cases, 1e-10 where PI is
// involved and inexact representation costs a few digits). We
// deliberately don't use 4-ULP comparison here — the goal is "the
// function works", not "bit-exact across LLVM versions".
//
// Compiled as C++ so WCS_TEST's static-initializer registration
// works (file-scope constant-init requirement of pure C). Includes
// only <math.h> + <wcs-test.h>; no libc++ involvement.

#include <math.h>
#include <wcs-test.h>

namespace {
constexpr double PI  = 3.14159265358979323846;
constexpr double E   = 2.71828182845904523536;
constexpr double EPS = 1e-12;
}  // namespace

WCS_TEST(LibM, sqrt) {
    WCS_CHECK_NEAR(sqrt(0.0), 0.0, EPS);
    WCS_CHECK_NEAR(sqrt(1.0), 1.0, EPS);
    WCS_CHECK_NEAR(sqrt(4.0), 2.0, EPS);
    WCS_CHECK_NEAR(sqrt(2.0), 1.4142135623730951, EPS);
    WCS_CHECK_NEAR(sqrt(1e6), 1000.0, EPS);
}

WCS_TEST(LibM, fabs) {
    WCS_CHECK_NEAR(fabs(0.0),  0.0, EPS);
    WCS_CHECK_NEAR(fabs(-1.5), 1.5, EPS);
    WCS_CHECK_NEAR(fabs(2.7),  2.7, EPS);
}

WCS_TEST(LibM, sin) {
    WCS_CHECK_NEAR(sin(0.0),    0.0, EPS);
    WCS_CHECK_NEAR(sin(PI / 2), 1.0, EPS);
    WCS_CHECK_NEAR(sin(PI),     0.0, 1e-10);   // PI is inexact
    WCS_CHECK_NEAR(sin(-PI / 2), -1.0, EPS);
}

WCS_TEST(LibM, cos) {
    WCS_CHECK_NEAR(cos(0.0),    1.0, EPS);
    WCS_CHECK_NEAR(cos(PI / 2), 0.0, 1e-10);
    WCS_CHECK_NEAR(cos(PI),    -1.0, EPS);
}

WCS_TEST(LibM, tan) {
    WCS_CHECK_NEAR(tan(0.0),    0.0, EPS);
    WCS_CHECK_NEAR(tan(PI / 4), 1.0, 1e-10);
}

WCS_TEST(LibM, asin) {
    WCS_CHECK_NEAR(asin(0.0),  0.0,    EPS);
    WCS_CHECK_NEAR(asin(1.0),  PI / 2, EPS);
    WCS_CHECK_NEAR(asin(-1.0), -PI / 2, EPS);
}

WCS_TEST(LibM, acos) {
    WCS_CHECK_NEAR(acos(1.0),  0.0,    EPS);
    WCS_CHECK_NEAR(acos(0.0),  PI / 2, EPS);
    WCS_CHECK_NEAR(acos(-1.0), PI,     EPS);
}

WCS_TEST(LibM, atan) {
    WCS_CHECK_NEAR(atan(0.0), 0.0,    EPS);
    WCS_CHECK_NEAR(atan(1.0), PI / 4, EPS);
}

WCS_TEST(LibM, atan2) {
    WCS_CHECK_NEAR(atan2( 0.0, 1.0), 0.0,    EPS);
    WCS_CHECK_NEAR(atan2( 1.0, 0.0), PI / 2, EPS);
    WCS_CHECK_NEAR(atan2( 1.0, 1.0), PI / 4, EPS);
    WCS_CHECK_NEAR(atan2(-1.0, 1.0), -PI / 4, EPS);
}

WCS_TEST(LibM, exp) {
    WCS_CHECK_NEAR(exp(0.0), 1.0,   EPS);
    WCS_CHECK_NEAR(exp(1.0), E,     1e-10);
    WCS_CHECK_NEAR(exp(2.0), E * E, 1e-10);
}

WCS_TEST(LibM, log) {
    WCS_CHECK_NEAR(log(1.0), 0.0, EPS);
    WCS_CHECK_NEAR(log(E),   1.0, 1e-10);
}

WCS_TEST(LibM, log10) {
    WCS_CHECK_NEAR(log10(1.0),     0.0, EPS);
    WCS_CHECK_NEAR(log10(10.0),    1.0, EPS);
    WCS_CHECK_NEAR(log10(100.0),   2.0, EPS);
    WCS_CHECK_NEAR(log10(1000.0),  3.0, EPS);
}

WCS_TEST(LibM, log2) {
    WCS_CHECK_NEAR(log2(1.0),     0.0,  EPS);
    WCS_CHECK_NEAR(log2(2.0),     1.0,  EPS);
    WCS_CHECK_NEAR(log2(1024.0), 10.0,  EPS);
}

WCS_TEST(LibM, pow) {
    WCS_CHECK_NEAR(pow(2.0,  0.0),    1.0, EPS);
    WCS_CHECK_NEAR(pow(2.0, 10.0), 1024.0, EPS);
    WCS_CHECK_NEAR(pow(3.0,  2.0),    9.0, EPS);
    WCS_CHECK_NEAR(pow(2.0, -1.0),    0.5, EPS);
}

WCS_TEST(LibM, hypot) {
    WCS_CHECK_NEAR(hypot(3.0, 4.0),                5.0, EPS);
    WCS_CHECK_NEAR(hypot(0.0, 0.0),                0.0, EPS);
    WCS_CHECK_NEAR(hypot(1.0, 1.0), 1.4142135623730951, EPS);
}

WCS_TEST(LibM, fma) {
    WCS_CHECK_NEAR(fma(2.0, 3.0, 4.0), 10.0, EPS);
    // Genuine fused-rounding case: 1e16 * 1.0 = 1e16 (exact), but
    // adding 1.0 in double precision normally rounds it away (1e16
    // and 1.0 differ by more than the mantissa width). fma rounds
    // ONCE on the (exact) full-width product+addend, preserving the
    // +1.0. A non-fused implementation returns 1e16 (wrong by 1).
    WCS_CHECK_NEAR(fma(1e16, 1.0, 1.0), 1e16 + 1.0, EPS);
}

WCS_TEST(LibM, fmax_fmin) {
    WCS_CHECK_NEAR(fmax( 1.0,  2.0),  2.0, EPS);
    WCS_CHECK_NEAR(fmax(-1.0, -2.0), -1.0, EPS);
    WCS_CHECK_NEAR(fmin( 1.0,  2.0),  1.0, EPS);
    WCS_CHECK_NEAR(fmin(-1.0, -2.0), -2.0, EPS);
}

WCS_TEST(LibM, ceil_floor) {
    WCS_CHECK_NEAR(ceil( 2.5),  3.0, EPS);
    WCS_CHECK_NEAR(ceil(-2.5), -2.0, EPS);
    WCS_CHECK_NEAR(floor( 2.5),  2.0, EPS);
    WCS_CHECK_NEAR(floor(-2.5), -3.0, EPS);
}

WCS_TEST(LibM, trunc_round) {
    WCS_CHECK_NEAR(trunc( 2.7),  2.0, EPS);
    WCS_CHECK_NEAR(trunc(-2.7), -2.0, EPS);
    // round() is half-away-from-zero per C99.
    WCS_CHECK_NEAR(round( 2.5),  3.0, EPS);
    WCS_CHECK_NEAR(round(-2.5), -3.0, EPS);
    WCS_CHECK_NEAR(round( 2.4),  2.0, EPS);
}

WCS_TEST(LibM, copysign) {
    WCS_CHECK_NEAR(copysign( 3.0, -1.0), -3.0, EPS);
    WCS_CHECK_NEAR(copysign(-3.0,  1.0),  3.0, EPS);
}

WCS_TEST(LibM, ldexp_scalbn) {
    WCS_CHECK_NEAR(ldexp(1.0,  10), 1024.0, EPS);
    WCS_CHECK_NEAR(ldexp(1.0, -10), 1.0 / 1024.0, EPS);
    WCS_CHECK_NEAR(scalbn(1.0, -1), 0.5, EPS);
    WCS_CHECK_NEAR(scalbn(3.0,  4), 48.0, EPS);
}

// fmod and frexp go through volatile function pointers: with constant
// arguments clang otherwise folds the calls away, and the test would pass
// without ever linking the archive's implementation.
static double (*volatile fmod_fn)(double, double) = fmod;
static double (*volatile frexp_fn)(double, int*) = frexp;

WCS_TEST(LibM, fmod) {
    WCS_CHECK_NEAR(fmod_fn(7.5, 2.0), 1.5, EPS);
    WCS_CHECK_NEAR(fmod_fn(-7.5, 2.0), -1.5, EPS);
    WCS_CHECK_NEAR(fmod_fn(1.0, 3.0), 1.0, EPS);
    WCS_CHECK_EQ(fmod_fn(5.0, INFINITY), 5.0);
    double negzero = fmod_fn(-0.0, 1.0);
    WCS_CHECK(negzero == 0.0 && signbit(negzero));
    WCS_CHECK(isnan(fmod_fn(1.0, 0.0)));
    WCS_CHECK(isnan(fmod_fn(INFINITY, 1.0)));
    WCS_CHECK(isnan(fmod_fn(NAN, 1.0)));
}

WCS_TEST(LibM, frexp) {
    int e = 0;
    WCS_CHECK_NEAR(frexp_fn(1024.0, &e), 0.5, EPS);
    WCS_CHECK_EQ(e, 11);
    WCS_CHECK_NEAR(frexp_fn(-3.0, &e), -0.75, EPS);
    WCS_CHECK_EQ(e, 2);
    double zero = frexp_fn(-0.0, &e);
    WCS_CHECK(zero == 0.0 && signbit(zero));
    WCS_CHECK_EQ(e, 0);
    // Smallest subnormal, 2^-1074 = 0.5 * 2^-1073.
    WCS_CHECK_EQ(frexp_fn(4.9406564584124654e-324, &e), 0.5);
    WCS_CHECK_EQ(e, -1073);
    WCS_CHECK(isinf(frexp_fn(INFINITY, &e)));
    WCS_CHECK(isnan(frexp_fn(NAN, &e)));
}

WCS_TEST(LibM, ilogb) {
    WCS_CHECK_EQ(ilogb(1.0),     0);
    WCS_CHECK_EQ(ilogb(2.0),     1);
    WCS_CHECK_EQ(ilogb(1024.0), 10);
    WCS_CHECK_EQ(ilogb(0.5),    -1);
}

WCS_TEST(LibM, remquo) {
    // C11 §7.12.10.3: *quo's magnitude need only match the integral
    // quotient modulo 2^n where n is implementation-defined ≥ 3.
    // Asserting the low 3 bits keeps the test vendor-portable —
    // musl returns the full quotient (3, 4) and so passes today,
    // but a future libm swap that returns a folded value still works.
    int quo = -999;
    double rem = remquo(10.0, 3.0, &quo);
    WCS_CHECK_NEAR(rem, 1.0, EPS);
    WCS_CHECK_EQ(quo & 0x7, 3);

    rem = remquo(7.0, 2.0, &quo);
    // 7 = 2*3 + 1, but remquo rounds-to-nearest: 7 = 2*4 - 1, so rem=-1, quo=4.
    WCS_CHECK_NEAR(rem, -1.0, EPS);
    WCS_CHECK_EQ(quo & 0x7, 4);
}
