#pragma once
// A minimal self-check helper for the exercise solutions:
//
//   DAWLAB_CHECK(peak < 1.0f);
//   DAWLAB_CHECK_NEAR(gainDb, -3.01, 0.05);
//   return dawlab::test::summary("ex1_gain");   // returns 0 if at least one check ran and every check passed, otherwise 1 (ctest then reports a failure)

#include "Console.h"

#include <cmath>
#include <cstdio>

namespace dawlab::test {

struct Counters {
    int checks = 0;
    int failures = 0;
};

inline Counters& counters() {
    static Counters c;
    return c;
}

inline void report(bool ok, const char* expr, const char* file, int line) {
    auto& c = counters();
    ++c.checks;
    if (!ok) {
        ++c.failures;
        std::printf("  [FAIL] %s  (%s:%d)\n", expr, file, line);
    }
}

inline void reportNear(double a, double b, double tol, const char* expr, const char* file, int line) {
    auto& c = counters();
    ++c.checks;
    if (!(std::abs(a - b) <= tol)) {
        ++c.failures;
        std::printf("  [FAIL] %s  actual %.6g, expected %.6g ± %.3g  (%s:%d)\n", expr, a, b, tol, file, line);
    }
}

// The one rule for a run: it passes only if at least one check ran and none failed. A test that skips all its
// checks (an early return, an empty loop, a branch not taken) proves nothing, so it fails. Pure, so a test can probe
// it with its own Counters.
inline int summaryOf(const Counters& c, const char* name) {
    if (c.checks == 0) {
        std::printf("[FAIL] %s (no check ran)\n", name);
        return 1;
    }
    if (c.failures == 0)
        std::printf("[PASS] %s (%d checks passed)\n", name, c.checks);
    else
        std::printf("[FAIL] %s (%d of %d checks failed)\n", name, c.failures, c.checks);
    return c.failures == 0 ? 0 : 1;
}

inline int summary(const char* name) {
    return summaryOf(counters(), name);
}

} // namespace dawlab::test

#define DAWLAB_CHECK(cond) ::dawlab::test::report(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define DAWLAB_CHECK_NEAR(a, b, tol)                                                                    \
    ::dawlab::test::reportNear(static_cast<double>(a), static_cast<double>(b), static_cast<double>(tol), \
                               #a " ≈ " #b, __FILE__, __LINE__)
