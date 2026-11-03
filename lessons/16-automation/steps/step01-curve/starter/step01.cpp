// Step 1 (starter): the automation curve.
//
// Build and run:  l16_step01_starter   (prints [FAIL] until all four TODOs are done)
// Reference:      ../solution/step01.cpp, docs/step-01-curve.md ("Your turn")
//
// AutomationPoint, AutomationCurve (its data members and the provided helpers segmentAt(),
// between() and clamp01()) are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

int AutomationCurve::add(AutomationPoint point) {
    // TODO 1: clamp the value to 0..1 (clamp01). Insert the point so that points_ stays sorted by
    //         position, AFTER every point that has the same position (std::upper_bound with a
    //         comparison on the position does exactly that). Return the index it got - from the
    //         iterator insert() returns, in a statement of its own (docs: "A bug only GCC showed").
    (void)point;
    return -1;
}

bool AutomationCurve::remove(int index) {
    // TODO 2: false if index is not a point; otherwise erase it and return true.
    (void)index;
    return false;
}

float AutomationCurve::valueAt(std::int64_t position) const noexcept {
    // TODO 3: no points: default_. Otherwise i = segmentAt(position): i < 0 means before the first
    //         point (its value), the last index means at or after the last point (its value),
    //         anything else is between point i and point i + 1: between(i, position).
    (void)position;
    return 0.0f;
}

void AutomationCurve::render(std::int64_t start, float* out, int n) const noexcept {
    // TODO 4: the same values as valueAt(start), valueAt(start + 1), ... but with ONE search:
    //         i = segmentAt(start) once, then for each sample move i forward while the next point
    //         is at or before the sample's position, and compute the value as in TODO 3.
    //         n <= 0 writes nothing; an empty curve writes default_ everywhere.
    (void)start;
    (void)out;
    (void)n;
}

} // namespace minidaw

#include "../check.h"
