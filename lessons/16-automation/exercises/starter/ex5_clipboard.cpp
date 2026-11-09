// Exercise 5 (starter): copy and paste automation.
//
// Fill in the two TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex5_clipboard.cpp. What the functions must do is in ../ex5_clipboard.h.

#include "../ex5_clipboard.h"

namespace minidaw {

CurveClip copyRange(const AutomationCurve& curve, std::int64_t start, std::int64_t end) {
    // TODO: length = end - start, the curve's default value, and the points: one at 0 (valueAt(start),
    //       shapeAt(start)), those strictly inside moved by -start, one at the length (valueBefore(end)).
    (void)curve;
    (void)start;
    (void)end;
    return {};
}

std::optional<Change> pasteChange(const Project& project, const AutomationTarget& target, const CurveClip& clip, std::int64_t at) {
    // TODO: resolve() the target (NoStrip / NoEffect -> nullopt; an empty clip too). The old curve is
    //       the lane's (findLane), or AutomationCurve(clip.defaultValue). Build the new curve with
    //       add(), in this order: old points before `at`; {at, valueBefore(old, at)}; the clip's
    //       points moved by +at; {at + length, old.valueAt(at + length), shapeAt(old, at + length)};
    //       old points after at + length. Return Change{SetLane{target, LaneContent{mode, curve}}}.
    (void)project;
    (void)target;
    (void)clip;
    (void)at;
    return std::nullopt;
}

} // namespace minidaw

#include "../ex5_check.h"
