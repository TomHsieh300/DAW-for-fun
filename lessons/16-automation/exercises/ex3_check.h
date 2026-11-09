#pragma once
// Exercise 3 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex3_curve_shape.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <limits>
#include <vector>

namespace ex3::check {

inline void checkFraction() {
    for (const float k : {-1.0f, -0.5f, 0.0f, 0.3f, 1.0f}) {
        DAWLAB_CHECK(curveFraction(0.0, k) == 0.0);
        DAWLAB_CHECK(curveFraction(1.0, k) == 1.0);
        bool rising = true, mirrored = true;
        double last = 0.0;
        for (int i = 0; i <= 1000; ++i) {
            const double t = i / 1000.0;
            const double f = curveFraction(t, k);
            rising = rising && f >= last && f >= 0.0 && f <= 1.0;
            last = f;
            mirrored = mirrored && std::abs(curveFraction(t, -k) - (1.0 - curveFraction(1.0 - t, k))) < 1e-12;
        }
        DAWLAB_CHECK(rising);
        DAWLAB_CHECK(mirrored);
    }
    bool exact = true;
    for (int i = 0; i <= 1000; ++i)
        exact = exact && curveFraction(i / 1000.0, 0.0f) == i / 1000.0; // tension 0 is the line, bit for bit
    DAWLAB_CHECK(exact);
    DAWLAB_CHECK_NEAR(curveFraction(0.5, 1.0f), 0.0625, 1e-12);  // 0.5^4
    DAWLAB_CHECK_NEAR(curveFraction(0.5, 0.5f), 0.25, 1e-12);    // 0.5^2
    DAWLAB_CHECK_NEAR(curveFraction(0.5, -1.0f), 0.9375, 1e-12); // 1 - 0.5^4
    DAWLAB_CHECK(curveFraction(0.3, 3.0f) == curveFraction(0.3, 1.0f));   // clamped
    DAWLAB_CHECK(curveFraction(0.3, -7.0f) == curveFraction(0.3, -1.0f));
    DAWLAB_CHECK(curveFraction(0.3, std::numeric_limits<float>::quiet_NaN()) == 0.3);
}

inline void checkCurve() {
    AutomationCurve c(0.5f);
    c.add({0, 0.2f, Shape::Curve, 0.5f});        // a slow rise
    c.add({48000, 0.8f, Shape::Linear});
    c.add({96000, 0.8f, Shape::Curve, -1.0f});   // flat: a curve between equal values stays flat
    c.add({120000, 0.8f, Shape::Curve, -1.0f});  // a fast fall
    c.add({168000, 0.4f, Shape::Hold});
    c.add({192000, 0.1f, Shape::Linear});
    c.add({192000, 0.9f, Shape::Curve, 1.0f});   // a jump, then a curve
    c.add({240000, 0.0f});

    DAWLAB_CHECK_NEAR(c.valueAt(24000), 0.2 + 0.6 * 0.25, 1e-6);
    DAWLAB_CHECK(c.valueAt(48000) == 0.8f); // exactly at the next point
    DAWLAB_CHECK_NEAR(c.valueAt(72000), 0.8, 1e-6);
    DAWLAB_CHECK(c.valueAt(100000) == 0.8f);
    DAWLAB_CHECK_NEAR(c.valueAt(144000), 0.8 - 0.4 * 0.9375, 1e-6);
    DAWLAB_CHECK(c.valueAt(180000) == 0.4f); // Hold
    DAWLAB_CHECK(c.valueAt(191999) == 0.4f);
    DAWLAB_CHECK(c.valueAt(192000) == 0.9f); // the jump
    DAWLAB_CHECK_NEAR(c.valueAt(216000), 0.9 - 0.9 * 0.0625, 1e-6);
    DAWLAB_CHECK(c.valueAt(240000) == 0.0f);

    // Never outside the two values of its segment.
    bool inside = true;
    for (std::int64_t p = 0; p < 48000; p += 7)
        inside = inside && c.valueAt(p) >= 0.2f && c.valueAt(p) <= 0.8f;
    for (std::int64_t p = 120000; p < 168000; p += 7)
        inside = inside && c.valueAt(p) >= 0.4f && c.valueAt(p) <= 0.8f;
    DAWLAB_CHECK(inside);

    // render() and valueAt() agree for every block size, every value.
    for (const int block : {1, 37, 512, 4096}) {
        std::vector<float> out(static_cast<size_t>(block));
        int wrong = 0;
        for (std::int64_t start = -100; start < 250000; start += block) {
            c.render(start, out.data(), block);
            for (int k = 0; k < block; ++k)
                wrong += out[static_cast<size_t>(k)] == c.valueAt(start + k) ? 0 : 1;
        }
        DAWLAB_CHECK(wrong == 0);
    }

    // Tension 0 is Linear, bit for bit.
    AutomationCurve line, curve;
    line.add({0, 0.1f, Shape::Linear});
    line.add({4801, 0.7f});
    curve.add({0, 0.1f, Shape::Curve, 0.0f});
    curve.add({4801, 0.7f});
    int different = 0;
    for (std::int64_t p = 0; p <= 4801; ++p)
        different += line.valueAt(p) == curve.valueAt(p) && curve.between(0, std::min<std::int64_t>(p, 4800)) == curve.linear(0, std::min<std::int64_t>(p, 4800)) ? 0 : 1;
    DAWLAB_CHECK(different == 0);
    // A tension on a Linear or a Hold point means nothing.
    AutomationCurve stray;
    stray.add({0, 0.0f, Shape::Linear, 0.7f});
    stray.add({1000, 1.0f, Shape::Hold, -0.7f});
    stray.add({2000, 0.0f});
    DAWLAB_CHECK(std::abs(stray.valueAt(500) - 0.5f) < 1e-6f && stray.valueAt(1500) == 1.0f);
    // between() on the two points of a jump: the first one's value, no division by zero.
    DAWLAB_CHECK(c.between(5, 192000) == 0.1f);
    std::printf("  a fade with tension 0.5 is at %.4f halfway (a line: 0.5000), with -1 at %.4f\n",
                static_cast<double>(curveFraction(0.5, 0.5f)), static_cast<double>(curveFraction(0.5, -1.0f)));
}

} // namespace ex3::check

int main() {
    std::printf("Exercise 3 - a curved segment with a tension\n");
    ex3::check::checkFraction();
    ex3::check::checkCurve();
    return dawlab::test::summary("l16_ex3");
}
