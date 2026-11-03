// Step 1 (solution): the automation curve.
//
// Build and run:  l16_step01_solution   (ctest, label "solution")
// Your version:   starter/step01.cpp -> l16_step01_starter

#include "../minidaw.h"

namespace minidaw {

int AutomationCurve::add(AutomationPoint point) {
    point.value = clamp01(point.value);
    // upper_bound: after every point at this position, so a second point there makes a jump.
    const auto at = std::upper_bound(points_.begin(), points_.end(), point.position,
                                     [](std::int64_t p, const AutomationPoint& a) { return p < a.position; });
    // Two statements on purpose: in `insert(...) - points_.begin()` the compiler may call begin()
    // first, and insert() may then move the points to new memory (GCC does exactly that).
    const auto inserted = points_.insert(at, point);
    return static_cast<int>(inserted - points_.begin());
}

bool AutomationCurve::remove(int index) {
    if (index < 0 || index >= size())
        return false;
    points_.erase(points_.begin() + index);
    return true;
}

float AutomationCurve::valueAt(std::int64_t position) const noexcept {
    if (points_.empty())
        return default_;
    const int i = segmentAt(position);
    if (i < 0)
        return points_.front().value; // before the first point
    if (i == size() - 1)
        return points_.back().value; // at or after the last point
    return between(i, position);
}

void AutomationCurve::render(std::int64_t start, float* out, int n) const noexcept {
    if (n <= 0)
        return;
    if (points_.empty()) {
        std::fill_n(out, n, default_);
        return;
    }
    int i = segmentAt(start); // the one search of the block
    const int last = size() - 1;
    std::int64_t position = start;
    for (int k = 0; k < n; ++k, ++position) {
        // Walk forward over every point the block reaches (all points of a jump at once).
        while (i < last && points_[static_cast<size_t>(i) + 1].position <= position)
            ++i;
        out[k] = i < 0 ? points_.front().value : (i == last ? points_.back().value : between(i, position));
    }
}

} // namespace minidaw

#include "../check.h"
