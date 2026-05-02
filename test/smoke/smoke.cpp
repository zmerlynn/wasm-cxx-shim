// test/smoke/smoke.cpp — STL-using smoke test.
//
// Exercises the canonical "vector + unordered_map + virtual dtor" path
// through libc++:
//   * std::vector<int> push_back / iteration → operator new[] (vector
//     growth), memcpy/memmove (relocation), operator delete[]
//   * std::unordered_map<int,int> insert / iterate → __next_prime (in
//     libcxx/src/stubs.cpp), bucket allocation through operator new
//   * Virtual class with virtual dtor through a base pointer →
//     libcxx vtable + std::exception key-function path
//   * std::sqrt + std::sin from <cmath> → libm
//
// Compiled with the consumer's libc++ headers + our __config_site
// override that disables threads/filesystem/locale/etc. Linked
// against the three shim components.
//
// Returns:
//   sum(1..5)            = 15
//   sum(squares 1..5)    = 1+4+9+16+25 = 55  (from unordered_map values)
//   Impl(7).compute()    = 7
//   sqrt(1024)           = 32
//   sin(0) * 1000        = 0
//   ───────────────────────────
//   total                = 109

#include <vector>
#include <unordered_map>
#include <cmath>

struct Base {
    virtual ~Base() = default;
    virtual int compute() const = 0;
};

struct Impl : Base {
    int v;
    Impl(int v_) : v(v_) {}
    int compute() const override { return v; }
};

extern "C" int run() {
    std::vector<int> nums;
    for (int i = 1; i <= 5; ++i) {
        nums.push_back(i);
    }
    int sum = 0;
    for (int n : nums) sum += n;                  // 15

    std::unordered_map<int, int> squares;
    for (int n : nums) squares[n] = n * n;
    int sumsq = 0;
    for (auto& kv : squares) sumsq += kv.second;  // 1+4+9+16+25 = 55

    Base* p = new Impl(7);
    int x = p->compute();                         // 7
    delete p;

    int s = static_cast<int>(std::sqrt(1024.0));  // 32
    int z = static_cast<int>(std::sin(0.0) * 1000.0);  // 0

    return sum + sumsq + x + s + z;               // 15 + 55 + 7 + 32 + 0 = 109
}
