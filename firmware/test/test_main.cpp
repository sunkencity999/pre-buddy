// Aggregating test runner.
#include <cstdio>

#include "test_harness.h"

int main() {
    auto& r = pre_test::registry();
    std::printf("Running %zu PRE Buddy host tests\n", r.size());
    for (auto& tc : r) {
        std::printf("- %s\n", tc.name);
        tc.fn();
    }
    int f = pre_test::failures();
    if (f == 0) {
        std::printf("ALL %zu TESTS PASSED\n", r.size());
        return 0;
    }
    std::fprintf(stderr, "%d FAILURE(S)\n", f);
    return 1;
}
