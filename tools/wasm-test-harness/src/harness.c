// tools/wasm-test-harness/src/harness.c
//
// Implementation of the wasm-test-harness primitives declared in
// include/wcs-test.h. C, not C++, so it can compile against the shim
// without dragging libcxx along (the harness uses C-level statics,
// not std::vector etc.).

#include "wcs-test.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

// Forward-declare the one libc function we use. (`__builtin_strlen`
// is a clang intrinsic; no declaration needed.)
extern void* memcpy(void* dst, const void* src, unsigned long n);

// Tiny printf-style formatter. Handles only the conversion specifiers
// the harness's own log_printf calls use:
//   %s         (const char*)
//   %d         (int)
//   %u         (unsigned int)
//   %zu        (size_t)
//   plus a numeric field-width prefix (`%4d`, `%5zu`)
//
// No %f, no %x, no precision specifiers, no '+' / ' ' flags. Adding
// them is a localized change to this function. Returning to a real
// vsnprintf would require pulling musl's stdio (multi-thousand-line
// transitive dep) and is out of scope for the shim; the harness's
// output needs are tiny so a hand-rolled formatter is the right move.
static int _wcs_vsnprintf(char* dst, unsigned long n, const char* fmt, va_list ap) {
    unsigned long w = 0;  // bytes written
    for (const char* p = fmt; *p; ) {
        if (*p != '%') {
            if (w + 1 < n) dst[w] = *p;
            ++w; ++p;
            continue;
        }
        ++p;  // past '%'
        // optional decimal field width
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            ++p;
        }
        // optional 'z' length modifier
        int is_z = 0;
        if (*p == 'z') { is_z = 1; ++p; }

        // emit the conversion
        char buf[32];
        const char* s = 0;
        unsigned long len = 0;

        if (*p == 's') {
            s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            len = (unsigned long)__builtin_strlen(s);
        } else if (*p == 'd') {
            // int (or zd treated same on wasm32)
            long v = is_z ? va_arg(ap, long) : (long)va_arg(ap, int);
            unsigned long uv = v < 0 ? (unsigned long)-v : (unsigned long)v;
            // build digits backward
            int bi = 31;
            buf[bi--] = 0;
            if (uv == 0) buf[bi--] = '0';
            while (uv) { buf[bi--] = '0' + (char)(uv % 10); uv /= 10; }
            if (v < 0) buf[bi--] = '-';
            s = &buf[bi + 1];
            len = (unsigned long)(31 - bi - 1);
        } else if (*p == 'u') {
            unsigned long uv = is_z ? va_arg(ap, unsigned long)
                                    : (unsigned long)va_arg(ap, unsigned int);
            int bi = 31;
            buf[bi--] = 0;
            if (uv == 0) buf[bi--] = '0';
            while (uv) { buf[bi--] = '0' + (char)(uv % 10); uv /= 10; }
            s = &buf[bi + 1];
            len = (unsigned long)(31 - bi - 1);
        } else {
            // unrecognized — emit literally for diagnostic
            if (w + 1 < n) dst[w] = '%';
            ++w;
            if (*p) {
                if (w + 1 < n) dst[w] = *p;
                ++w; ++p;
            }
            continue;
        }
        ++p;  // past conversion char

        // pad
        if ((unsigned long)width > len) {
            unsigned long pad = (unsigned long)width - len;
            for (unsigned long k = 0; k < pad; ++k) {
                if (w + 1 < n) dst[w] = ' ';
                ++w;
            }
        }
        for (unsigned long k = 0; k < len; ++k) {
            if (w + 1 < n) dst[w] = s[k];
            ++w;
        }
    }
    if (n > 0) dst[w < n ? w : n - 1] = 0;
    return (int)w;
}

// ---- Registry --------------------------------------------------------------

#define WCS_MAX_TESTS 4096

struct wcs_test_entry {
    const char* suite;
    const char* name;
    wcs_test_fn fn;
};

static struct wcs_test_entry wcs_tests[WCS_MAX_TESTS];
static size_t                wcs_test_n = 0;

int wcs_register_test(const char* suite, const char* name, wcs_test_fn fn) {
    if (wcs_test_n >= WCS_MAX_TESTS) return 0;  /* silently drop overflow */
    wcs_tests[wcs_test_n].suite = suite;
    wcs_tests[wcs_test_n].name  = name;
    wcs_tests[wcs_test_n].fn    = fn;
    ++wcs_test_n;
    return (int)wcs_test_n;
}

size_t      wcs_test_count(void)            { return wcs_test_n; }
const char* wcs_test_suite(size_t i)        { return i < wcs_test_n ? wcs_tests[i].suite : ""; }
const char* wcs_test_name(size_t i)         { return i < wcs_test_n ? wcs_tests[i].name  : ""; }
wcs_test_fn wcs_test_fn_at(size_t i)        { return i < wcs_test_n ? wcs_tests[i].fn    : (wcs_test_fn)0; }

// ---- Per-test failure counter ---------------------------------------------

static int wcs_failures_in_current_test = 0;

int  wcs_current_failures(void) { return wcs_failures_in_current_test; }

// ---- Log buffer ------------------------------------------------------------

#define WCS_LOG_CAP (1 << 18)  /* 256 KB */

static char   wcs_log[WCS_LOG_CAP];
static size_t wcs_log_n = 0;

const char* wcs_log_buffer(void) { return wcs_log; }
size_t      wcs_log_size(void)   { return wcs_log_n; }

int wcs_log_printf(const char* fmt, ...) {
    if (wcs_log_n >= WCS_LOG_CAP) return 0;
    va_list ap;
    va_start(ap, fmt);
    int avail = (int)(WCS_LOG_CAP - wcs_log_n);
    int n = _wcs_vsnprintf(wcs_log + wcs_log_n, (unsigned long)avail, fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if (n > avail) n = avail;
    wcs_log_n += (size_t)n;
    return n;
}

// Append a fixed string. Used by the runner. Faster than vsnprintf.
static void wcs_log_str(const char* s) {
    if (wcs_log_n >= WCS_LOG_CAP) return;
    int len = __builtin_strlen(s);
    int avail = (int)(WCS_LOG_CAP - wcs_log_n);
    if (len > avail) len = avail;
    memcpy(wcs_log + wcs_log_n, s, (unsigned long)len);
    wcs_log_n += (size_t)len;
}

// ---- Failure-recording -----------------------------------------------------

void wcs_record_failure(const char* file, int line, const char* expr) {
    ++wcs_failures_in_current_test;
    wcs_log_printf("    FAIL %s:%d: %s\n", file, line, expr);
}

// ---- Runner ----------------------------------------------------------------

int wcs_run_tests(void) {
    int passed = 0, failed = 0;
    wcs_log_str("wasm-test-harness: starting...\n");
    for (size_t i = 0; i < wcs_test_n; ++i) {
        wcs_failures_in_current_test = 0;
        wcs_log_printf("[%4zu/%4zu] %s.%s ... ", i + 1, wcs_test_n,
                       wcs_tests[i].suite, wcs_tests[i].name);
        wcs_tests[i].fn();
        if (wcs_failures_in_current_test == 0) {
            wcs_log_str("ok\n");
            ++passed;
        } else {
            wcs_log_printf("FAILED (%d assertion failures)\n",
                           wcs_failures_in_current_test);
            ++failed;
        }
    }
    wcs_log_printf("wasm-test-harness: %d passed, %d failed, %zu total\n",
                   passed, failed, wcs_test_n);
    return failed;
}
