/* Vendored verbatim from wasi-libc's musl tree.
 * Source: https://github.com/WebAssembly/wasi-libc/blob/main/libc-top-half/musl/src/include/features.h
 * License: MIT (see LICENSES/LICENSE-musl).
 *
 * NOTE: this is the PRIVATE features.h that overrides the public one
 * for musl's own internal compilation (adds `hidden`/`weak`/`weak_alias`
 * macros). It must come BEFORE the public include/ on the include path.
 * See libm/CMakeLists.txt's `BEFORE PRIVATE` keyword for the wiring.
 */
#ifndef FEATURES_H
#define FEATURES_H

#include "../../include/features.h"

#define weak __attribute__((__weak__))
#define hidden __attribute__((__visibility__("hidden")))
#define weak_alias(old, new) \
	extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))

#endif
