// Exercise 3 (solution): a curved segment with a tension.
//
// Build and run:  l16_ex3_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex3_curve_shape.cpp -> l16_ex3_starter

#include "../exercises/ex3_curve_shape.h"

namespace ex3 {

double curveFraction(double t, float tension) noexcept {
    // NaN fails both comparisons and becomes 0: test what IS allowed (Step 1's clamp01 does the same).
    const double k = tension >= -1.0f ? (tension <= 1.0f ? tension : 1.0) : (tension < -1.0f ? -1.0 : 0.0);
    const double e = std::pow(4.0, std::abs(k));
    if (k >= 0.0)
        return std::pow(t, e); // e == 1: pow(t, 1) is t exactly
    return 1.0 - std::pow(1.0 - t, e);
}

float AutomationCurve::between(int i, std::int64_t position) const noexcept {
    const AutomationPoint& a = points_[static_cast<size_t>(i)];
    const AutomationPoint& b = points_[static_cast<size_t>(i) + 1];
    if (a.shape == Shape::Hold || b.position == a.position)
        return a.value;
    const double t = static_cast<double>(position - a.position) / static_cast<double>(b.position - a.position);
    const double f = a.shape == Shape::Curve ? curveFraction(t, a.tension) : t;
    return static_cast<float>(a.value + (static_cast<double>(b.value) - a.value) * f);
}

} // namespace ex3

#include "../exercises/ex3_check.h"
