#pragma once
// Exercise 3 - A curved segment with a tension.
//
// Step 1 knows two shapes: Linear and Hold. A fade-in sounds better with a curve that starts slowly
// and a fade-out with one that falls fast at first. Most DAWs give each segment a TENSION:
//
//   value                                    tension > 0: slow start, fast end
//   1.0 |                       ..*          tension = 0: the straight line, exactly
//       |                    .'  /           tension < 0: fast start, slow end
//       |                 .'   /  .'
//       |              .'    / .'
//       |          ..'    /.'
//   0.0 *.......''    /.'
//       +-----------------------------  position
//
// With t = (position - x_i) / (x_{i+1} - x_i) in 0..1 and k = tension in -1..1, the fraction of the
// way from v_i to v_{i+1} is
//
//   f(t) = t^e                  for k >= 0       e = 4^|k|  (1 .. 4)
//   f(t) = 1 - (1 - t)^e        for k <  0
//
// so k and -k are mirror images of each other: f_{-k}(t) = 1 - f_k(1 - t). The curve still starts
// exactly at v_i and ends exactly at v_{i+1}, never leaves the range between them, and k = 0 gives
// exactly the values Linear gives.
//
// The curve class is Step 1's, solved, with Shape::Curve and a tension per point added. valueAt()
// and render() both go through between(), so they agree as long as between() does.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ex3 {

enum class Shape {
    Linear, // a straight line to the next point
    Hold,   // the value stays until the next point
    Curve,  // a curve to the next point, bent by the point's tension
};

struct AutomationPoint {
    std::int64_t position = 0;
    float value = 0.0f;
    Shape shape = Shape::Linear;
    float tension = 0.0f; // Curve only: -1 .. 1
};

// ---- YOUR TURN (starter/ex3_curve_shape.cpp) ------------------------------------------------------
// f(t) for t in 0..1 (see above). The tension is clamped to -1..1, and NaN counts as 0.
double curveFraction(double t, float tension) noexcept;

class AutomationCurve {
public:
    explicit AutomationCurve(float defaultValue = 0.0f) : default_(clamp01(defaultValue)) {}

    // ---- YOUR TURN ----------------------------------------------------------------------------
    // The value between point i and point i + 1 at `position` (p_i.position <= position <
    // p_{i+1}.position): Hold -> v_i; Linear -> Step 1's formula, unchanged; Curve ->
    // v_i + (v_{i+1} - v_i) * curveFraction(t, p_i.tension). Two points at one position: v_i.
    float between(int i, std::int64_t position) const noexcept;

    // ---- provided: Step 1, solved ------------------------------------------------------------------
    int add(AutomationPoint point) {
        point.value = clamp01(point.value);
        const auto at = std::upper_bound(points_.begin(), points_.end(), point.position,
                                         [](std::int64_t p, const AutomationPoint& a) { return p < a.position; });
        const auto inserted = points_.insert(at, point);
        return static_cast<int>(inserted - points_.begin());
    }
    float valueAt(std::int64_t position) const noexcept {
        if (points_.empty())
            return default_;
        const int i = segmentAt(position);
        if (i < 0)
            return points_.front().value;
        if (i == size() - 1)
            return points_.back().value;
        return between(i, position);
    }
    void render(std::int64_t start, float* out, int n) const noexcept {
        if (n <= 0)
            return;
        if (points_.empty()) {
            std::fill_n(out, n, default_);
            return;
        }
        int i = segmentAt(start);
        const int last = size() - 1;
        std::int64_t position = start;
        for (int k = 0; k < n; ++k, ++position) {
            while (i < last && points_[static_cast<size_t>(i) + 1].position <= position)
                ++i;
            out[k] = i < 0 ? points_.front().value : (i == last ? points_.back().value : between(i, position));
        }
    }
    int segmentAt(std::int64_t position) const noexcept {
        const auto after = std::upper_bound(points_.begin(), points_.end(), position,
                                            [](std::int64_t p, const AutomationPoint& a) { return p < a.position; });
        return static_cast<int>(after - points_.begin()) - 1;
    }
    const std::vector<AutomationPoint>& points() const noexcept { return points_; }
    int size() const noexcept { return static_cast<int>(points_.size()); }
    static float clamp01(float v) noexcept { return v >= 0.0f ? (v <= 1.0f ? v : 1.0f) : 0.0f; }

    // Step 1's straight line, for comparison.
    float linear(int i, std::int64_t position) const noexcept {
        const AutomationPoint& a = points_[static_cast<size_t>(i)];
        const AutomationPoint& b = points_[static_cast<size_t>(i) + 1];
        if (b.position == a.position)
            return a.value;
        const double t = static_cast<double>(position - a.position) / static_cast<double>(b.position - a.position);
        return static_cast<float>(a.value + (static_cast<double>(b.value) - a.value) * t);
    }

private:
    std::vector<AutomationPoint> points_;
    float default_;
};

} // namespace ex3
