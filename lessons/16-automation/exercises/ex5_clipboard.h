#pragma once
// Exercise 5 - Copy and paste automation.
//
// Select bars 5-8 of the filter's curve, copy, click bar 17, paste: the same movement again. Or paste
// it onto another parameter. Two functions, on Step 4's model (model.h):
//
//   copyRange(curve, start, end)        the curve's values on [start, end) as a CurveClip: points
//                                       relative to `start`, a point at 0 and one at the length
//   pasteChange(project, target, clip, at)
//                                       the Change (one SetLane) that puts the clip at `at`
//
// What "the same values" means, sample for sample (the self-check tests exactly this):
//
//   after.valueAt(p) == before.valueAt(p)                  for p <  at            (exactly)
//   after.valueAt(p) == source.valueAt(start + p - at)     for at <= p < at + len (to 1e-6)
//   after.valueAt(p) == before.valueAt(p)                  for p >= at + len      (to 1e-6)
//
// so a paste may need a jump at `at` and at `at + len`, and a point in the middle of a segment of
// the old curve (where the clip starts or ends) to keep the old values next to it.

#include "model.h"

#include <optional>
#include <vector>

namespace minidaw {

// A piece of a curve, as the clipboard holds it.
struct CurveClip {
    std::int64_t length = 0;                // > 0
    std::vector<AutomationPoint> points;    // positions 0 .. length, sorted
    float defaultValue = 0.0f;              // the source curve's, for a lane that does not exist yet
};

// ---- provided -----------------------------------------------------------------------------------
// The value the curve heads for as it reaches `position` from the left: the same as valueAt() except
// at a jump, where it is the value BEFORE the jump (and where a segment ends, the value at its end).
inline float valueBefore(const AutomationCurve& curve, std::int64_t position) {
    const auto& pts = curve.points();
    if (pts.empty())
        return curve.defaultValue();
    // The last point strictly before `position`.
    const auto at = std::lower_bound(pts.begin(), pts.end(), position,
                                     [](const AutomationPoint& a, std::int64_t p) { return a.position < p; });
    if (at == pts.begin())
        return pts.front().value;
    const AutomationPoint& a = *(at - 1);
    if (at == pts.end() || a.shape == Shape::Hold)
        return a.value;
    const AutomationPoint& b = *at; // b.position >= position
    const double t = static_cast<double>(position - a.position) / static_cast<double>(b.position - a.position);
    return static_cast<float>(a.value + (static_cast<double>(b.value) - a.value) * t);
}

// The shape a new point at `position` must have so that the curve goes on as before: Hold if the
// segment there holds.
inline Shape shapeAt(const AutomationCurve& curve, std::int64_t position) {
    const auto& pts = curve.points();
    const auto after = std::upper_bound(pts.begin(), pts.end(), position,
                                        [](std::int64_t p, const AutomationPoint& a) { return p < a.position; });
    return after != pts.begin() && (after - 1)->shape == Shape::Hold ? Shape::Hold : Shape::Linear;
}

// ---- YOUR TURN (starter/ex5_clipboard.cpp) -------------------------------------------------------
// The curve on [start, end), end > start:
//   a point at 0 with valueAt(start) and shapeAt(start);
//   every point with start < position < end, moved by -start (shapes kept, jumps kept);
//   a point at end - start with valueBefore(end) (where the last segment arrives).
CurveClip copyRange(const AutomationCurve& curve, std::int64_t start, std::int64_t end);

// The change that pastes `clip` at `at` into the lane of `target` (one SetLane, so one undo step).
// The lane keeps its mode; without a lane, a new one in Read mode with the clip's default value.
// nullopt if the target's strip or effect does not exist (resolve() says NoStrip or NoEffect) or the
// clip is empty. The new curve:
//   the old points before `at`, then a point at `at` with valueBefore(old, at) (the old line ends there);
//   the clip's points, moved by +at;
//   a point at at + length with old.valueAt(at + length) and shapeAt(old, at + length);
//   the old points after at + length.
std::optional<Change> pasteChange(const Project& project, const AutomationTarget& target, const CurveClip& clip, std::int64_t at);

} // namespace minidaw
