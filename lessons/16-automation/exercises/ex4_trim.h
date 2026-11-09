#pragma once
// Exercise 4 - Trim: move a curve up and down instead of replacing it.
//
// Step 3's Touch, Latch and Write record what the hand does and REPLACE the curve with it. The mix
// engineer who likes a vocal's ride but wants the second chorus 2 dB hotter needs something else: a
// fifth mode, TRIM, where the fader is an OFFSET. Moving it up adds to the curve that is there; the
// shape of the ride stays. The recording works as in Step 3 - the audio thread stamps the knob's
// value, the UI groups it into a pass and thins it - only the pass holds offsets, and it is merged
// into the curve instead of punched:
//
//   old curve          ____/‾‾‾‾\____          pass (offsets)       __/‾‾‾‾‾‾‾\__  (0, +0.2, 0)
//   after the trim     ____/‾‾‾‾\____  with the middle part 0.2 higher, its shape unchanged
//
// This exercise writes the merge, on Step 4's curve (Linear and Hold):
//
//   applyTrim(old, pass)      the curve whose value at p is clamp01(old(p) + offset(p)) for p from the
//                             pass's first to its last position, and old(p) outside
//   trimChange(project, target, pass)
//                             the same as one undoable SetLane
//
// Between two points both the old curve (Linear: a line; Hold: a constant) and the offset (a line
// through the pass's points) are straight lines, and so is their sum. So the new curve needs a point
// wherever either has one - and two (before and after) where the old curve jumps or a hold ends.

#include "model.h"

#include <optional>
#include <vector>

namespace minidaw {

struct TrimPoint {
    std::int64_t position = 0;
    float offset = 0.0f; // normalized, -1 .. 1
};

// ---- provided -----------------------------------------------------------------------------------
// The value the curve heads for as it reaches `position` from the left (at a jump: before it).
inline float valueBefore(const AutomationCurve& curve, std::int64_t position) {
    const auto& pts = curve.points();
    if (pts.empty())
        return curve.defaultValue();
    const auto at = std::lower_bound(pts.begin(), pts.end(), position,
                                     [](const AutomationPoint& a, std::int64_t p) { return a.position < p; });
    if (at == pts.begin())
        return pts.front().value;
    const AutomationPoint& a = *(at - 1);
    if (at == pts.end() || a.shape == Shape::Hold)
        return a.value;
    const AutomationPoint& b = *at;
    const double t = static_cast<double>(position - a.position) / static_cast<double>(b.position - a.position);
    return static_cast<float>(a.value + (static_cast<double>(b.value) - a.value) * t);
}
// Hold if the segment at `position` holds.
inline Shape shapeAt(const AutomationCurve& curve, std::int64_t position) {
    const auto& pts = curve.points();
    const auto after = std::upper_bound(pts.begin(), pts.end(), position,
                                        [](std::int64_t p, const AutomationPoint& a) { return p < a.position; });
    return after != pts.begin() && (after - 1)->shape == Shape::Hold ? Shape::Hold : Shape::Linear;
}
// The pass's offset at `position` (first <= position <= last): the line through its points.
inline float offsetAt(const std::vector<TrimPoint>& pass, std::int64_t position) {
    const auto after = std::upper_bound(pass.begin(), pass.end(), position,
                                        [](std::int64_t p, const TrimPoint& a) { return p < a.position; });
    if (after == pass.begin())
        return pass.front().offset;
    if (after == pass.end())
        return pass.back().offset;
    const TrimPoint& a = *(after - 1);
    const TrimPoint& b = *after;
    const double t = static_cast<double>(position - a.position) / static_cast<double>(b.position - a.position);
    return static_cast<float>(a.offset + (static_cast<double>(b.offset) - a.offset) * t);
}

// ---- YOUR TURN (starter/ex4_trim.cpp) ----------------------------------------------------------
// `pass`: at least two points, positions strictly increasing (as a thinned pass is). With
// first = pass.front().position and last = pass.back().position, the new curve is, in this order:
//   - the old points before `first`, and {first, valueBefore(old, first)}: the old line ends there;
//   - at every position x with first <= x <= last where the old curve or the pass has a point
//     (first and last included), one point clamp01(old + offset(x)) - or two, if
//     valueBefore(old, x) != old.valueAt(x): the one before the jump, then the one after it.
//     All Linear;
//   - {last, old.valueAt(last), shapeAt(old, last)}: the old curve again, and the old points after last.
AutomationCurve applyTrim(const AutomationCurve& old, const std::vector<TrimPoint>& pass);

// The trim as one SetLane (the lane keeps its mode). nullopt if there is no lane for `target` (there
// is nothing to trim) or the pass has fewer than two points.
std::optional<Change> trimChange(const Project& project, const AutomationTarget& target, const std::vector<TrimPoint>& pass);

} // namespace minidaw
