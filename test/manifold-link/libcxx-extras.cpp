// test/manifold-link/libcxx-extras.cpp
//
// Provides the libc++ source-file symbols that manifold pulls in but
// the main libcxx component intentionally doesn't ship:
//   * std::__1::__shared_count / __shared_weak_count out-of-line
//     methods (from libc++'s src/memory.cpp)
//   * std::nothrow global, std::__throw_bad_alloc (from libc++'s
//     src/new_helpers.cpp)
//   * std::bad_weak_ptr key functions
//   * std::align helper
//   * std::chrono::steady_clock::now (stub; see below)
//   * std::pmr memory resources and unsynchronized_pool_resource
//     (from libc++'s src/memory_resource.cpp; see below)
//
// These are documented in libcxx/README.md as "consumers will surface
// link errors and need to compile [the upstream libc++ source files]
// themselves." This file IS that compilation, scoped to the manifold-
// link smoke. It's NOT part of the main libcxx component because it
// requires `#include <memory>` / `<new>` which we deliberately keep
// out of the main libcxx stubs (insulation from libc++ version
// drift). Including those headers couples this TU to libc++'s
// `__shared_count` / `__shared_weak_count` class layout — if libc++
// changes those layouts in a future release, this TU silently breaks
// (the manifold-tests run will catch it). We accept that coupling
// because the alternative — redeclaring the classes locally with
// matching layouts — is fragile in a different way.
//
// Runtime semantics: refcount manipulation is non-atomic (correct
// for our single-threaded wasm), and the destructor + __release_weak
// dispatch through the vtable to derived-class
// `__on_zero_shared_weak()` implementations — which DO free the
// controlled object. Lifetime correctness holds for single-threaded
// use; do not assume it generalizes to a future threaded build.
//
// References (the upstream files we're emulating):
//   https://github.com/llvm/llvm-project/blob/release/20.x/libcxx/src/memory.cpp
//   https://github.com/llvm/llvm-project/blob/release/20.x/libcxx/src/new_helpers.cpp

#include <memory>
#include <new>
#include <typeinfo>

// ---- std::nothrow + __throw_bad_alloc (from new_helpers.cpp) ----

namespace std {  // intentionally NOT versioned (matches libc++ upstream)

const nothrow_t nothrow{};

[[noreturn]] void __throw_bad_alloc() {
    __builtin_trap();
}

}  // namespace std

// ---- bad_weak_ptr key functions + shared_count/__shared_weak_count
//      out-of-line methods (from memory.cpp) ----

namespace std { inline namespace __1 {

bad_weak_ptr::~bad_weak_ptr() noexcept {}
const char* bad_weak_ptr::what() const noexcept { return "bad_weak_ptr"; }

__shared_count::~__shared_count() {}
__shared_weak_count::~__shared_weak_count() {}

void __shared_weak_count::__release_weak() noexcept {
    if (--__shared_weak_owners_ == -1) {
        __on_zero_shared_weak();
    }
}

__shared_weak_count* __shared_weak_count::lock() noexcept {
    long object_owners = __shared_owners_;
    while (object_owners != -1) {
        // single-threaded: just bump and return
        __shared_owners_ = object_owners + 1;
        return this;
    }
    return nullptr;
}

const void* __shared_weak_count::__get_deleter(const type_info&) const noexcept {
    return nullptr;
}

// std::align (rare-use helper, vendored from upstream verbatim)
void* align(size_t alignment, size_t size, void*& ptr, size_t& space) {
    void* r = nullptr;
    if (size <= space) {
        char* p1 = static_cast<char*>(ptr);
        char* p2 = reinterpret_cast<char*>(
            reinterpret_cast<__UINTPTR_TYPE__>(p1 + (alignment - 1)) & -alignment);
        size_t d = static_cast<size_t>(p2 - p1);
        if (d <= space - size) {
            r = p2;
            ptr = r;
            space -= d;
        }
    }
    return r;
}

// std::__hash_memory — libc++'s out-of-line memory hash, used by every
// hash-container instantiation (unordered_map, unordered_set, ...).
// Under LLVM 20 the symbol was elsewhere or inlined; LLVM 21 makes it
// an undefined extern that libc++ expects the runtime to provide. The
// shim doesn't ship libc++.a/.dylib, so we provide it here. Quality
// (avalanche, distribution) doesn't have to match libc++'s upstream
// implementation byte-for-byte — hash containers stay correct as long
// as the function is deterministic and a function of all input bytes.
// FNV-1a fits the bill in ~10 lines. Uses 32-bit constants because
// size_t is 32-bit on wasm32.
//
// LLVM 21 declares `__hash_memory` in `<__functional/hash.h>` with a
// `_LIBCPP_NOESCAPE` parameter attribute (expands to
// `[[_Clang::__noescape__]]`). Definition must match the declaration's
// parameter attributes or clang flags "conflicting types". We pull in
// the declaration if it exists (LLVM 21+) and use `_LIBCPP_NOESCAPE`
// in the definition (macro is defined unconditionally in `<__config>`
// across LLVM 20+, expanding to either the attribute or nothing).
}}  // close std::__1 to include libc++ headers at translation-unit scope

#if __has_include(<__functional/hash.h>)
#  include <__functional/hash.h>
#endif

namespace std { inline namespace __1 {

_LIBCPP_EXPORTED_FROM_ABI size_t __hash_memory(_LIBCPP_NOESCAPE const void* key, size_t length) noexcept {
    constexpr size_t fnv_offset_basis = 2166136261u;
    constexpr size_t fnv_prime        = 16777619u;
    size_t h = fnv_offset_basis;
    const unsigned char* p = static_cast<const unsigned char*>(key);
    for (size_t i = 0; i < length; ++i) {
        h ^= static_cast<size_t>(p[i]);
        h *= fnv_prime;
    }
    return h;
}

}}  // namespace std::__1

// ---- std::chrono::steady_clock::now (from chrono.cpp) ----
//
// manifold's Boolean2 timing diagnostics (src/boolean2_diagnostics.h)
// name std::chrono::steady_clock unconditionally, so the __config_site
// override turns on _LIBCPP_HAS_MONOTONIC_CLOCK to declare it. There is
// no clock on wasm32-unknown-unknown (and reading one would need a WASI
// import), so now() always returns the epoch. manifold only calls it
// when its timing diagnostics are switched on, which they are not by
// default; with this stub they report zero durations, not wrong results.
#include <chrono>

namespace std { inline namespace __1 { namespace chrono {

steady_clock::time_point steady_clock::now() noexcept { return time_point(); }

}}}  // namespace std::__1::chrono

// ---- std::pmr (from memory_resource.cpp) ----
//
// manifold's Boolean2 engine allocates through
// std::pmr::unsynchronized_pool_resource. libc++ declares it in
// <memory_resource> but defines it in src/memory_resource.cpp. This is a
// reduced port of that file for single-threaded use:
//   * memory_resource's destructor, new_delete_resource(),
//     null_memory_resource(), get/set_default_resource() as upstream,
//     without the atomic/mutex paths;
//   * unsynchronized_pool_resource routes every request through its
//     "adhoc" pool (one upstream allocation per request, all freed by
//     release()) instead of the size-class fixed pools. That is a valid
//     pool resource, just a less memory-efficient one, and it keeps the
//     port to the members the header declares.
// Like the shared_ptr internals above, this couples to libc++'s private
// layout for the class; the manifold-tests run catches a mismatch.
//
// The adhoc-pool code below is from libc++'s src/memory_resource.cpp:
//   https://github.com/llvm/llvm-project/blob/release/20.x/libcxx/src/memory_resource.cpp
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <memory_resource>

namespace std { inline namespace __1 { namespace pmr {

memory_resource::~memory_resource() = default;

namespace {

class new_delete_resource_imp final : public memory_resource {
    void* do_allocate(size_t bytes, size_t align) override {
        return ::operator new(bytes, std::align_val_t(align));
    }
    void do_deallocate(void* p, size_t, size_t align) override {
        ::operator delete(p, std::align_val_t(align));
    }
    bool do_is_equal(const memory_resource& other) const noexcept override {
        return &other == this;
    }
};

class null_resource_imp final : public memory_resource {
    void* do_allocate(size_t, size_t) override { __builtin_trap(); }
    void do_deallocate(void*, size_t, size_t) override {}
    bool do_is_equal(const memory_resource& other) const noexcept override {
        return &other == this;
    }
};

new_delete_resource_imp new_delete_res;
null_resource_imp null_res;
memory_resource* default_res = &new_delete_res;

size_t roundup(size_t count, size_t alignment) {
    size_t mask = alignment - 1;
    return (count + mask) & ~mask;
}

}  // namespace

memory_resource* new_delete_resource() noexcept { return &new_delete_res; }
memory_resource* null_memory_resource() noexcept { return &null_res; }
memory_resource* get_default_resource() noexcept { return default_res; }

memory_resource* set_default_resource(memory_resource* new_res) noexcept {
    memory_resource* old = default_res;
    default_res = new_res ? new_res : &new_delete_res;
    return old;
}

struct unsynchronized_pool_resource::__adhoc_pool::__chunk_footer {
    __chunk_footer* __next_;
    char* __start_;
    size_t __align_;
    size_t __allocation_size() { return (reinterpret_cast<char*>(this) - __start_) + sizeof(*this); }
};

void unsynchronized_pool_resource::__adhoc_pool::__release_ptr(memory_resource* upstream) {
    while (__first_ != nullptr) {
        __chunk_footer* next = __first_->__next_;
        upstream->deallocate(__first_->__start_, __first_->__allocation_size(), __first_->__align_);
        __first_ = next;
    }
}

void* unsynchronized_pool_resource::__adhoc_pool::__do_allocate(memory_resource* upstream, size_t bytes, size_t align) {
    const size_t footer_size  = sizeof(__chunk_footer);
    const size_t footer_align = alignof(__chunk_footer);

    if (align < footer_align)
        align = footer_align;

    size_t aligned_capacity = roundup(bytes, footer_align) + footer_size;

    void* result = upstream->allocate(aligned_capacity, align);

    __chunk_footer* h = (__chunk_footer*)((char*)result + aligned_capacity - footer_size);
    h->__next_        = __first_;
    h->__start_       = (char*)result;
    h->__align_       = align;
    __first_          = h;
    return result;
}

void unsynchronized_pool_resource::__adhoc_pool::__do_deallocate(
    memory_resource* upstream, void* p, size_t, size_t) {
    if (__first_ == nullptr)
        __builtin_trap();  // not allocated with this resource
    if (__first_->__start_ == p) {
        __chunk_footer* next = __first_->__next_;
        upstream->deallocate(p, __first_->__allocation_size(), __first_->__align_);
        __first_ = next;
        return;
    }
    for (__chunk_footer* h = __first_; h->__next_ != nullptr; h = h->__next_) {
        if (h->__next_->__start_ == p) {
            __chunk_footer* next = h->__next_->__next_;
            upstream->deallocate(p, h->__next_->__allocation_size(), h->__next_->__align_);
            h->__next_ = next;
            return;
        }
    }
    __builtin_trap();  // not allocated with this resource
}

// wasm-cxx-shim: no fixed pools. __fixed_pools_ stays null and
// __num_fixed_pools_ only records the size classes options() reports.
unsynchronized_pool_resource::unsynchronized_pool_resource(const pool_options& opts, memory_resource* upstream)
    : __res_(upstream), __fixed_pools_(nullptr), __num_fixed_pools_(0) {
    __options_max_blocks_per_chunk_ =
        opts.max_blocks_per_chunk == 0 ? __max_blocks_per_chunk : opts.max_blocks_per_chunk;
}

pool_options unsynchronized_pool_resource::options() const {
    pool_options p;
    p.max_blocks_per_chunk        = __options_max_blocks_per_chunk_;
    p.largest_required_pool_block = 0;
    return p;
}

void unsynchronized_pool_resource::release() { __adhoc_pool_.__release_ptr(__res_); }

void* unsynchronized_pool_resource::do_allocate(size_t bytes, size_t align) {
    return __adhoc_pool_.__do_allocate(__res_, bytes, align);
}

void unsynchronized_pool_resource::do_deallocate(void* p, size_t bytes, size_t align) {
    __adhoc_pool_.__do_deallocate(__res_, p, bytes, align);
}

}}}  // namespace std::__1::pmr
