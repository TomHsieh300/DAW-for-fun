#pragma once
// Exercise 9 - A lane menu: thin, clear, remove (optional, JUCE).
//
// A curve drawn point by point, or recorded with a very small tolerance, ends up with more points than
// it needs; a stretch of it should go; a lane is not wanted any more. Three commands for a lane's menu,
// each one undo step, without changing the app:
//
//   thinCurve(curve, tolerance)     Step 3's thinning for a whole curve. Step 3 thins one pass, whose
//                                   positions strictly increase. A curve has jumps (two points at one
//                                   position) and holds, and those must come out exactly as they were.
//                                   So the anchors stay: the first and last point, every Hold point and
//                                   the point after it, both points of a jump. Between two anchors the
//                                   points are a plain line run, and engine::thin() thins that.
//   clearRange(curve, start, end)   no points strictly between start and end: a straight line from the
//                                   curve's value at start to the value it reaches at end
//   addLaneItems(menu, session, lane)
//                                   "Thin Points", "Clear Between Locators" (the loop's locators) and
//                                   "Remove Lane", each enabled only when it would change something

#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ex9 {

constexpr float kThinTolerance = 0.002f; // Step 3's recording tolerance

// ---- provided -----------------------------------------------------------------------------------
// The value the curve heads for as it reaches `position` from the left (at a jump: before it).
inline float valueBefore(const dawlab::engine::AutomationCurve& curve, std::int64_t position) {
    const auto& pts = curve.points();
    if (pts.empty())
        return curve.defaultValue();
    const auto at = std::lower_bound(pts.begin(), pts.end(), position,
                                     [](const dawlab::engine::AutomationPoint& a, std::int64_t p) { return a.position < p; });
    if (at == pts.begin())
        return pts.front().value;
    const dawlab::engine::AutomationPoint& a = *(at - 1);
    if (at == pts.end() || a.shape == dawlab::engine::Shape::Hold)
        return a.value;
    const dawlab::engine::AutomationPoint& b = *at;
    const double t = static_cast<double>(position - a.position) / static_cast<double>(b.position - a.position);
    return static_cast<float>(a.value + (static_cast<double>(b.value) - a.value) * t);
}

// ---- YOUR TURN (starter/ex9_lane_menu.cpp) -------------------------------------------------------
dawlab::engine::AutomationCurve thinCurve(const dawlab::engine::AutomationCurve& curve, float tolerance);
dawlab::engine::AutomationCurve clearRange(const dawlab::engine::AutomationCurve& curve, std::int64_t start, std::int64_t end);

// Adds the three items. Each action performs one SetLane named "Thin <parameter>", "Clear <parameter>"
// and "Remove lane <parameter>". Disabled: Thin when it would not remove a point, Clear without a
// lane or without a loop range (loop end <= loop start) or with no point between the locators, Remove
// without a lane.
void addLaneItems(juce::PopupMenu& menu, dawlab::app::Session& session, const dawlab::app::LaneAddress& lane);

} // namespace ex9
