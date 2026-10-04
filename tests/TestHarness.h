#pragma once

// Minimal test runner: TEST(name) { CHECK(expr); CHECK_NEAR(a, b, eps); }

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, fn}); }
};

inline void fail(const char* file, int line, const std::string& msg) {
    ++failures();
    std::printf("  FAILED %s:%d: %s\n", file, line, msg.c_str());
}

} // namespace test

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name)                                                                       \
    static void TEST_CAT(test_fn_, __LINE__)();                                          \
    static test::Registrar TEST_CAT(test_reg_, __LINE__)(name, TEST_CAT(test_fn_, __LINE__)); \
    static void TEST_CAT(test_fn_, __LINE__)()

#define CHECK(expr)                                                                      \
    do {                                                                                 \
        if (!(expr)) test::fail(__FILE__, __LINE__, #expr);                              \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                            \
    do {                                                                                 \
        double va_ = (a), vb_ = (b);                                                     \
        if (!(std::fabs(va_ - vb_) <= (eps)))                                            \
            test::fail(__FILE__, __LINE__,                                               \
                       std::string(#a " ~= " #b " (") + std::to_string(va_) + " vs " +   \
                           std::to_string(vb_) + ")");                                   \
    } while (0)
