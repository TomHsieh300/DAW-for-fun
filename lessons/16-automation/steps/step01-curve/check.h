#pragma once
// check.h - the self-check of Step 1 (the automation curve). Defines main(); included at the END
// of starter/step01.cpp and solution/step01.cpp, so both run exactly the same checks.
//
// The worked example of docs/step-01-curve.md, the edge cases (empty curve, before the first and
// after the last point, jumps, clamping), and then random curves against a reference that is
// written as plainly as possible: a linear search over the points.

#include <dawlab/Common.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <vector>

namespace step01_check {

using namespace minidaw;

// A deterministic random generator (the same sequence on every compiler).
struct Lcg {
    std::uint32_t state = 16u;
    std::uint32_t next() noexcept {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    int below(int n) noexcept { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
    float unit() noexcept { return static_cast<float>(next() % 10001u) / 10000.0f; }
};

// The curve's definition, written as a plain search: the last point at or before the position,
// then the formula of minidaw.h. Slow on purpose - it is what the fast code must agree with.
inline float reference(const AutomationCurve& c, std::int64_t position) {
    const auto& p = c.points();
    if (p.empty())
        return c.defaultValue();
    int last = -1;
    for (int i = 0; i < static_cast<int>(p.size()); ++i)
        if (p[static_cast<size_t>(i)].position <= position)
            last = i;
    if (last < 0)
        return p.front().value;
    if (last == static_cast<int>(p.size()) - 1)
        return p.back().value;
    const AutomationPoint& a = p[static_cast<size_t>(last)];
    const AutomationPoint& b = p[static_cast<size_t>(last) + 1];
    if (a.shape == Shape::Hold)
        return a.value;
    const double t = static_cast<double>(position - a.position) / static_cast<double>(b.position - a.position);
    return static_cast<float>(a.value + (static_cast<double>(b.value) - a.value) * t);
}

// Safe accessors for checks and printing: they must not crash while the starter is unfinished.
inline float valueOf(const AutomationCurve& c, int i) {
    return i >= 0 && i < c.size() ? c.points()[static_cast<size_t>(i)].value : NAN;
}
inline std::int64_t positionOf(const AutomationCurve& c, int i) {
    return i >= 0 && i < c.size() ? c.points()[static_cast<size_t>(i)].position : -1;
}

inline bool sorted(const AutomationCurve& c) {
    const auto& p = c.points();
    for (size_t i = 1; i < p.size(); ++i)
        if (p[i - 1].position > p[i].position)
            return false;
    return true;
}

inline int run() {
    std::printf("Step 1: the automation curve (build-along)\n");

    // ---- the worked example: a fade in, a hold, a ramp down ------------------------------------
    //   0 s: 0.0 (linear) -> 1 s: 1.0 (hold) -> 2 s: 0.5 (linear) -> 3 s: 0.25    at 48 kHz
    AutomationCurve curve(0.7f);
    DAWLAB_CHECK(curve.valueAt(12345) == 0.7f); // empty: the default
    // added out of order on purpose: add() keeps them sorted
    curve.add({96000, 0.5f, Shape::Linear});
    curve.add({0, 0.0f, Shape::Linear});
    curve.add({144000, 0.25f, Shape::Linear});
    const int holdIndex = curve.add({48000, 1.0f, Shape::Hold});
    const std::int64_t at[] = {-100, 0, 24000, 47999, 48000, 72000, 95999, 96000, 120000, 144000, 500000};
    const float expected[] = {0.0f, 0.0f, 0.5f, 0.99997917f, 1.0f, 1.0f, 1.0f, 0.5f, 0.375f, 0.25f, 0.25f};
    std::printf("  4 points added out of order: sorted %s, the hold point at index %d (expected yes, 1)\n",
                sorted(curve) ? "yes" : "no", holdIndex);
    DAWLAB_CHECK(curve.size() == 4 && sorted(curve) && holdIndex == 1);
    std::printf("  valueAt:");
    bool exampleRight = true;
    for (size_t i = 0; i < std::size(at); ++i) {
        const float v = curve.valueAt(at[i]);
        std::printf(" %lld:%.6g", static_cast<long long>(at[i]), static_cast<double>(v));
        exampleRight = exampleRight && std::abs(v - expected[i]) < 1e-6f;
    }
    std::printf("\n  (expected 0 0 0.5 0.999979 1 1 1 0.5 0.375 0.25 0.25: before the first point the first\n"
                "   value, a hold until the next point, after the last point the last value)\n");
    DAWLAB_CHECK(exampleRight);

    // ---- a jump: two points at one position ------------------------------------------------------
    AutomationCurve jump;
    jump.add({0, 0.0f, Shape::Linear});
    jump.add({1000, 0.2f, Shape::Linear});
    const int second = jump.add({1000, 0.8f, Shape::Linear}); // after the first one at 1000
    jump.add({2000, 0.8f, Shape::Linear});
    std::printf("  jump at 1000: valueAt(999) %.4f, valueAt(1000) %.4f (expected 0.1998, 0.8000), second point at %d\n",
                static_cast<double>(jump.valueAt(999)), static_cast<double>(jump.valueAt(1000)), second);
    DAWLAB_CHECK(second == 2 && valueOf(jump, 1) == 0.2f && valueOf(jump, 2) == 0.8f);
    DAWLAB_CHECK(std::abs(jump.valueAt(999) - 0.1998f) < 1e-6f && jump.valueAt(1000) == 0.8f);

    // ---- values are normalized; remove ---------------------------------------------------------
    AutomationCurve c2;
    c2.add({10, 1.7f, Shape::Linear});
    c2.add({20, -0.3f, Shape::Linear});
    c2.add({30, std::nanf(""), Shape::Linear});
    std::printf("  values 1.7, -0.3, NaN stored as %.1f, %.1f, %.1f (expected 1.0, 0.0, 0.0)\n",
                static_cast<double>(valueOf(c2, 0)), static_cast<double>(valueOf(c2, 1)), static_cast<double>(valueOf(c2, 2)));
    DAWLAB_CHECK(valueOf(c2, 0) == 1.0f && valueOf(c2, 1) == 0.0f && valueOf(c2, 2) == 0.0f);
    const bool removedMiddle = c2.remove(1);
    const bool removedNothing = c2.remove(5) || c2.remove(-1);
    DAWLAB_CHECK(removedMiddle && !removedNothing && c2.size() == 2 && positionOf(c2, 1) == 30);

    // ---- render() gives what valueAt() gives, block by block -------------------------------------
    {
        float block[300];
        int wrong = 0;
        for (std::int64_t start = -500; start < 160000; start += 257) {
            curve.render(start, block, 300);
            for (int i = 0; i < 300; ++i)
                wrong += block[i] == curve.valueAt(start + i) ? 0 : 1;
        }
        float tail[5] = {-1, -1, -1, -1, -1};
        AutomationCurve empty(0.3f);
        empty.render(7, tail, 4);
        curve.render(0, tail, 0); // n = 0: nothing written
        std::printf("  render() over 160000 samples in blocks of 300: %d values differ from valueAt() (expected 0)\n", wrong);
        DAWLAB_CHECK(wrong == 0);
        DAWLAB_CHECK(tail[0] == 0.3f && tail[3] == 0.3f && tail[4] == -1.0f);
    }

    // ---- random curves against the reference ---------------------------------------------------
    {
        Lcg rng;
        int curves = 0, mismatches = 0, unsorted = 0, values = 0;
        std::vector<float> out(512);
        for (int round = 0; round < 300; ++round) {
            AutomationCurve c(rng.unit());
            const int n = rng.below(12);
            for (int k = 0; k < n; ++k) {
                // positions from a small range, so equal positions (jumps) happen often
                const AutomationPoint p{static_cast<std::int64_t>(rng.below(40)) * 100, rng.unit(),
                                        rng.below(3) == 0 ? Shape::Hold : Shape::Linear};
                c.add(p);
            }
            if (n > 3 && rng.below(2) == 0)
                c.remove(rng.below(c.size()));
            unsorted += sorted(c) ? 0 : 1;
            ++curves;
            for (int probe = 0; probe < 20; ++probe) {
                const std::int64_t pos = static_cast<std::int64_t>(rng.below(4400)) - 200;
                mismatches += c.valueAt(pos) == reference(c, pos) ? 0 : 1;
                ++values;
            }
            const std::int64_t start = static_cast<std::int64_t>(rng.below(4400)) - 200;
            const int len = 1 + rng.below(512);
            c.render(start, out.data(), len);
            for (int i = 0; i < len; ++i, ++values)
                mismatches += out[static_cast<size_t>(i)] == reference(c, start + i) ? 0 : 1;
        }
        std::printf("  %d random curves (jumps and holds included), %d values: %d differ from the reference,\n"
                    "  %d curves unsorted (expected 0, 0)\n",
                    curves, values, mismatches, unsorted);
        DAWLAB_CHECK(mismatches == 0);
        DAWLAB_CHECK(unsorted == 0);
    }

    return dawlab::test::summary("l16_step01");
}

} // namespace step01_check

int main() { return step01_check::run(); }
