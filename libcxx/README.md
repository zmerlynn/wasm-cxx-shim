# libcxx — C++ runtime subset

Provides the libc++ and libc++abi symbols required by C++ code on
`wasm32-unknown-unknown` that uses standard library types (`vector`,
`unordered_map`, virtual classes, etc.).

These are the symbols LLVM's `libc++` and `libc++abi` would normally
provide. They aren't shipped for `wasm32-unknown-unknown` because LLVM
only packages them for `wasm32-wasi` (via WASI SDK) and
`wasm32-unknown-emscripten` (via Emscripten).

## Symbols provided

ABI-level (libc++abi territory):
- `__cxa_atexit` — registers static-storage destructors. Returns 0
  ("registered"); destructors are never actually run on wasm.
- `__cxa_pure_virtual` — `__builtin_trap()` on call.
- `__cxa_throw` — `__builtin_trap()`. Documented below.
- `__cxa_throw_bad_array_new_length` — `__builtin_trap()`.
- `__dso_handle` — opaque static-storage handle, address-only.

C++ runtime support (libc++ territory):
- 4 × `operator new`: default, array, `std::align_val_t`, `nothrow_t`.
  Failure → trap (default/array/align_val) or null (nothrow).
- 7 × `operator delete`: default, array, `std::align_val_t`, plus
  sized variants for each (C++14 sized deallocation). All forward to
  `free()`.
- `std::__1::__libcpp_verbose_abort` — libc++'s assertion-handler
  entry. Traps.
- `std::exception::~exception()`, `std::exception::what()`. Defining
  the destructor out-of-line emits the vtable + typeinfo
  automatically (Itanium "key function" rule). Sibling
  `std::bad_exception` provided pre-emptively.

## Symbols NOT provided (and why)

- A real `__cxa_throw` with stack unwinding. Out of scope; v0.x policy
  per `docs/context.md`. Consumers compile `-fno-exceptions` so explicit
  `throw expr;` paths are stripped at compile time. Implicit STL throws
  (`std::vector::resize` → `std::bad_alloc`, etc.) become unrecoverable
  traps — acceptable on wasm.
- RTTI machinery (`__cxa_demangle`, `__class_type_info`, etc.).
  Consumers compile `-fno-rtti`.
- Thread-local storage (`__tls_*`). No threads on default
  `wasm32-unknown-unknown`.
- `nothrow` array variant, sized-array nothrow, etc. Add by demand.

### Symbols you may need to provide yourself

The original recipe ([pca006132's manifold-on-wasm
experiment](https://github.com/elalish/manifold/discussions/1046#discussioncomment-11302257))
required compiling four libc++ source TUs from the consumer's LLVM
source tree alongside the consumer's own code:

| File | Provides |
|---|---|
| [`libcxx/src/bind.cpp`](https://github.com/llvm/llvm-project/blob/main/libcxx/src/bind.cpp) | `std::placeholders::_1` … `_10` (the inline-namespace globals, not just the types) |
| [`libcxx/src/hash.cpp`](https://github.com/llvm/llvm-project/blob/main/libcxx/src/hash.cpp) | `std::__1::__next_prime` (hash-bucket sizing for unordered_map/set) |
| [`libcxx/src/memory.cpp`](https://github.com/llvm/llvm-project/blob/main/libcxx/src/memory.cpp) | `std::shared_ptr` reference-count atomics, `__shared_count::~__shared_count`, `bad_weak_ptr::~bad_weak_ptr` |
| [`libcxx/src/new_helpers.cpp`](https://github.com/llvm/llvm-project/blob/main/libcxx/src/new_helpers.cpp) | the `std::nothrow` global, `std::__throw_bad_alloc()` |

The smoke test in this repo replaces the `hash.cpp` dependency with a
~15-line trial-division `__next_prime` stub in `libcxx/src/stubs.cpp`,
which is sufficient for `std::unordered_map` to link. **The other
three files' symbols are NOT stubbed.** A consumer using `std::bind`
placeholders, `std::shared_ptr`, or `std::nothrow` (the global, not the
type) will surface new missing symbols at link time and either:

1. Compile the missing libc++ TU(s) into their own build (cleanest;
   they need libc++ source available, e.g. via FetchContent against
   llvm-project).
2. Stub the specific symbol they hit — fine for `std::nothrow` (a
   single global), worse for the shared_ptr atomics (real
   functionality).

This trade-off is deliberate for v0.1 to avoid making the shim's smoke
build depend on a 100+ MB llvm-project checkout. See `docs/plan.md`
Phase 4b decision log.

## ABI mangling reference

Two specific signatures are known traps. Verified empirically.

| C++ name | Mangled | Namespace |
|---|---|---|
| `std::__1::__libcpp_verbose_abort(char const*, ...)` | `_ZNSt3__122__libcpp_verbose_abortEPKcz` | **versioned** `std::__1::` |
| `std::exception::~exception()` (D0/D1/D2) | `_ZNSt9exceptionD{0,1,2}Ev` | **un**versioned `std::` |
| `vtable for std::exception` | `_ZTVSt9exception` | unversioned (auto-emitted) |

The unversioned-vs-versioned namespace distinction matters. Mangling
`std::exception::~exception` as `_ZNSt3__19exception...` produces a
symbol the consumer's link does NOT reference, and the link fails
with a confusing missing-symbol error.

`noexcept` is **not** part of Itanium mangling for non-member
functions, so a stub omitting `noexcept` still satisfies a reference
from an LLVM-21 libc++ that declared the function `noexcept`. We
omit `<__verbose_abort>` from `verbose_abort.cpp` for that reason.

## Implementation policy

- **Stub TUs do not include libc++ headers.** Header-free pattern
  insulates us from libc++ version drift across LLVM 16 → main.
- **Class re-declared locally** in `exception.cpp` rather than
  including `<exception>`. Itanium ABI mangling is name-based; same
  symbols, no header conflict.
- **`std::nothrow_t` and `std::align_val_t` re-declared locally** in
  `operator_new_delete.cpp`. Same reasoning. Editor LSPs that have
  libc++ on their include path may flag redefinition warnings — those
  are LSP false-positives; the cross-build is correct.

## What this depends on

- `malloc`, `free`, `aligned_alloc` — typically from `wasm-cxx-shim::libc`,
  but the dependency is *consumer-fulfilled*, not CMake-expressed. A
  consumer may satisfy these from any other source (custom allocator,
  Emscripten's emmalloc, etc.) and skip our libc.
- `__builtin_trap` for the trap stubs (clang intrinsic, no library).

## Component independence

This component does **not** link `wasm-cxx-shim::libc` or
`wasm-cxx-shim::libm`. The malloc/free dependency is documented here
and declared inline in `operator_new_delete.cpp` (without `#include`-ing
any libc header). A consumer can swap libc for a custom allocator
without touching libcxx.

## What about exceptions?

Out of scope, deliberately. Recommended consumer pattern: compile
with `-fno-exceptions`, accepting that:
- Explicit `throw` paths are eliminated at compile time.
- Implicit STL throws (`std::vector::resize` → `bad_alloc`, etc.) hit
  our `__cxa_throw` trap stub — an unrecoverable crash on the failing
  code path.

For most wasm consumers this is fine: an `OOM` is unrecoverable in
the embedder anyway, and other implicit throws (e.g., `regex_error`)
indicate programming errors that should crash.

If a future consumer needs a real exception runtime, that's a
meaningfully larger project (port libc++abi's unwind machinery to wasm)
and is explicit out-of-scope for v0.x.

## Replacing this component

CMake target: `wasm-cxx-shim::libcxx`. Drop it
(`-DWASM_CXX_SHIM_BUILD_LIBCXX=OFF`) and provide your own runtime
stubs. The libc and libm components are unaffected.

A future scenario where this becomes redundant: someone packages a
real, full libc++ port for `wasm32-unknown-unknown`. At that point
this component is no longer needed.
