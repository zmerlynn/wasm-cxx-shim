// libcxx/src/exception.cpp — out-of-line key functions for std::exception.
//
// Provides the symbols a consumer's link will reference whenever any
// type derives from std::exception (which a lot of STL code does
// implicitly through std::bad_alloc, std::bad_array_new_length, etc.).
//
// Class shape mirrors:
//   https://github.com/llvm/llvm-project/blob/main/libcxx/include/__exception/exception.h
// Out-of-line definitions match libcxxabi's:
//   https://github.com/llvm/llvm-project/blob/main/libcxxabi/src/stdlib_exception.cpp
//
// Critical ABI details (verified empirically and via libc++ headers):
//
//   * std::exception lives in the UNVERSIONED `std::` namespace, NOT
//     `std::__1::`. Mangled: `_ZNSt9exceptionD2Ev` (NOT
//     `_ZNSt3__19exception...`). Get this wrong and the link fails.
//
//   * The Itanium "key function" rule says: the TU defining the first
//     non-inline virtual function emits the vtable + typeinfo as a
//     side effect. ~exception() is that key function. Defining it here
//     gives us, automatically:
//         _ZTVSt9exception   (vtable for std::exception)
//         _ZTISt9exception   (typeinfo for std::exception)
//         _ZTSSt9exception   (typeinfo name)
//
//   * We re-declare class `exception` locally rather than `#include
//     <exception>`. Including the libc++ header would re-declare the
//     class with version-specific visibility/availability attributes
//     and risk conflicts. Mangling is determined by the qualified
//     name only (Itanium ABI), so the local class declaration with
//     the same shape produces the same symbol names.

namespace std {

class exception {
public:
    exception() noexcept = default;
    exception(const exception&) noexcept = default;
    exception& operator=(const exception&) noexcept = default;
    virtual ~exception() noexcept;
    virtual const char* what() const noexcept;
};

exception::~exception() noexcept {}
const char* exception::what() const noexcept { return "std::exception"; }

// bad_exception follows the same key-function rule and is in the same
// header upstream; provide it now to forestall the next round of
// missing-symbol churn. Tiny.
class bad_exception : public exception {
public:
    bad_exception() noexcept = default;
    bad_exception(const bad_exception&) noexcept = default;
    bad_exception& operator=(const bad_exception&) noexcept = default;
    ~bad_exception() noexcept override;
    const char* what() const noexcept override;
};

bad_exception::~bad_exception() noexcept {}
const char* bad_exception::what() const noexcept { return "std::bad_exception"; }

}  // namespace std
