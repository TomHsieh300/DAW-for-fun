// Exercise 3 (starter): a curved segment with a tension.
//
// Fill in the two TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex3_curve_shape.cpp. The formula is at the top of ../ex3_curve_shape.h.

#include "../ex3_curve_shape.h"

namespace ex3 {

double curveFraction(double t, float tension) noexcept {
    // TODO: clamp the tension to -1..1 (NaN -> 0); e = 4^|k|; k >= 0: t^e, k < 0: 1 - (1 - t)^e.
    (void)tension;
    return t;
}

float AutomationCurve::between(int i, std::int64_t position) const noexcept {
    // TODO: Step 1's between(), plus Shape::Curve: the fraction curveFraction(t, tension) instead of t.
    //       For now everything but Hold is a straight line.
    if (points_[static_cast<size_t>(i)].shape == Shape::Hold)
        return points_[static_cast<size_t>(i)].value;
    return linear(i, position);
}

} // namespace ex3

#include "../ex3_check.h"
