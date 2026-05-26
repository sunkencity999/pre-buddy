// SPDX-License-Identifier: TBD
// Minimal zero-dep test harness for PRE Buddy host tests.
// Each test file defines tests via PRE_TEST(name) { ... }. The harness
// registers them in a static list and runs them all from main().

#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace pre_test {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline int& failure_counter() {
    static int f = 0;
    return f;
}

inline void bump_failure() { ++failure_counter(); }
inline int failures() { return failure_counter(); }

}  // namespace pre_test

#define PRE_TEST(NAME)                                                   \
    static void NAME();                                                  \
    static ::pre_test::Registrar registrar_##NAME(#NAME, &NAME);         \
    static void NAME()

#define PRE_CHECK(COND)                                                          \
    do {                                                                         \
        if (!(COND)) {                                                           \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__,       \
                         #COND);                                                 \
            ::pre_test::bump_failure();                                          \
        }                                                                        \
    } while (0)

#define PRE_CHECK_EQ(A, B)                                                       \
    do {                                                                         \
        auto _a = (A);                                                           \
        auto _b = (B);                                                           \
        if (!(_a == _b)) {                                                       \
            std::fprintf(stderr, "  FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, \
                         #A, #B);                                                \
            ::pre_test::bump_failure();                                          \
        }                                                                        \
    } while (0)

#define PRE_CHECK_NEAR(A, B, EPS)                                                \
    do {                                                                         \
        double _a = static_cast<double>(A);                                      \
        double _b = static_cast<double>(B);                                      \
        if (std::fabs(_a - _b) > (EPS)) {                                        \
            std::fprintf(stderr, "  FAIL %s:%d: |%s - %s| <= %s\n",              \
                         __FILE__, __LINE__, #A, #B, #EPS);                      \
            ::pre_test::bump_failure();                                          \
        }                                                                        \
    } while (0)
