/* libc/include/time.h — minimal <time.h> for wasm-cxx-shim.
 *
 * Provides type names libc++'s <ctime> + <chrono> reference. Function
 * declarations are intentionally absent — wasm32-unknown-unknown has
 * no clock syscalls. Consumer code that calls time()/clock_gettime()
 * gets a link error from this shim, which is the correct outcome.
 */
#ifndef _WASM_CXX_SHIM_TIME_H
#define _WASM_CXX_SHIM_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_size_t
#include <bits/alltypes.h>

typedef long      time_t;
typedef long      clock_t;
typedef long long suseconds_t;
typedef long      clockid_t;

struct timespec { time_t tv_sec; long tv_nsec; };
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};

#define CLOCKS_PER_SEC 1000000L

#ifdef __cplusplus
}
#endif

#endif
