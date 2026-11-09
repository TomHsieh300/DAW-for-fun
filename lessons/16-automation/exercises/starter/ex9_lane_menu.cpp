// Exercise 9 (starter): a lane menu - thin, clear, remove.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex9_lane_menu.cpp. What each function must do is in ../ex9_lane_menu.h.
// Build with -Juce: .\scripts\build.ps1 -Only 16-automation -Juce

#include "../ex9_lane_menu.h"

#include <vector>

using namespace dawlab;
using namespace dawlab::app;

namespace ex9 {

engine::AutomationCurve thinCurve(const engine::AutomationCurve& curve, float tolerance) {
    // TODO: mark the anchors (first, last, Hold points and the point after each, both points of a jump);
    //       between two anchors, engine::thin() the run (anchors included); add the kept points in order.
    (void)tolerance;
    return curve;
}

engine::AutomationCurve clearRange(const engine::AutomationCurve& curve, std::int64_t start, std::int64_t end) {
    // TODO: end <= start -> the curve. Otherwise a pass of two points: {start, valueAt(start)} and
    //       {end, valueBefore(end)}, punched in (engine::punch).
    (void)start;
    (void)end;
    return curve;
}

void addLaneItems(juce::PopupMenu& menu, Session& session, const LaneAddress& lane) {
    // TODO: three juce::PopupMenu::Item with setEnabled() and setAction(): "Thin Points (n -> m)",
    //       "Clear Between Locators" (getEngine().getTransport().getLoopStart() / getLoopEnd()) and
    //       "Remove Lane"; each action one session.perform(edit::SetLane{...}, name).
    (void)menu;
    (void)session;
    (void)lane;
}

} // namespace ex9

#include "../ex9_check.h"
