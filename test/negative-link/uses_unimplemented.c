// test/negative-link/uses_unimplemented.c
//
// Calls a representative sample of declared-but-unimplemented libc
// functions. Exists to be DELIBERATELY UNBUILDABLE: the corresponding
// expect-link-failure.sh wrapper asserts that compiling+linking this
// TU into wasm fails. If the build ever succeeds, someone added an
// implementation that shouldn't be there — and our README's "calling
// these produces a link-time error" claim has silently regressed.
//
// Two categories of out-of-scope functions covered:
//   1. Declared by our shipped headers (<stdio.h>, <stdlib.h> from
//      upstream musl) but unimplemented in our libc. Compile
//      succeeds; link fails with undefined symbol.
//   2. NOT declared by our headers (we ship no <unistd.h>/<time.h>
//      function declarations). Reach them via extern declarations
//      so the compile step also succeeds and the link is the thing
//      that fails.

#include <stdio.h>
#include <stdlib.h>

// Category 2: extern declarations bypass the missing-from-headers
// check; the link still has nothing to resolve them against.
extern int  clock_gettime(int, void*);
extern int  open(const char*, int, ...);
extern int  read(int, void*, unsigned long);
extern int  close(int);

// Keep-alive: prevents the optimizer from concluding these calls are
// dead and removing them. Without it, -Os may strip the calls and
// the link would succeed because nothing references the symbols.
static void keep_alive(volatile void* p) { (void)p; }

void unimplemented_calls(void) {
    // Category 1: stdio.h declarations w/ no implementations.
    FILE* f = fopen("/tmp/x", "r");
    keep_alive(f);
    fclose(f);
    remove("/tmp/x");
    rename("/tmp/x", "/tmp/y");

    // Category 1: stdlib.h declarations w/ no implementations.
    int rc = system("ls");
    keep_alive(&rc);

    // Category 2: POSIX surface not declared by our headers.
    clock_gettime(0, (void*)0);
    int fd = open("/tmp/x", 0);
    char buf[16];
    read(fd, buf, sizeof(buf));
    close(fd);
}
