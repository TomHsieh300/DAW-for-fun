// Exercise 5 (solution): copy and paste automation.
//
// Build and run:  l16_ex5_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex5_clipboard.cpp -> l16_ex5_starter

#include "../exercises/ex5_clipboard.h"

namespace minidaw {

CurveClip copyRange(const AutomationCurve& curve, std::int64_t start, std::int64_t end) {
    CurveClip clip;
    clip.length = end - start;
    clip.defaultValue = curve.defaultValue();
    clip.points.push_back({0, curve.valueAt(start), shapeAt(curve, start)});
    for (const AutomationPoint& p : curve.points())
        if (p.position > start && p.position < end)
            clip.points.push_back({p.position - start, p.value, p.shape});
    clip.points.push_back({clip.length, valueBefore(curve, end), Shape::Linear});
    return clip;
}

std::optional<Change> pasteChange(const Project& project, const AutomationTarget& target, const CurveClip& clip, std::int64_t at) {
    const TargetStatus status = resolve(project, target);
    if (status == TargetStatus::NoStrip || status == TargetStatus::NoEffect || clip.length <= 0 || clip.points.empty())
        return std::nullopt;
    const AutomationLaneState* lane = findLane(project, target);
    const AutomationCurve old = lane != nullptr ? lane->curve : AutomationCurve(clip.defaultValue);
    const std::int64_t end = at + clip.length;

    // add() keeps equal positions in the order they are added: that is how the jumps come out right.
    AutomationCurve curve(old.defaultValue());
    for (const AutomationPoint& p : old.points())
        if (p.position < at)
            curve.add(p);
    curve.add({at, valueBefore(old, at), Shape::Linear}); // the old line, up to where the clip begins
    for (const AutomationPoint& p : clip.points)
        curve.add({p.position + at, p.value, p.shape});
    curve.add({end, old.valueAt(end), shapeAt(old, end)}); // the old curve again, from where the clip ends
    for (const AutomationPoint& p : old.points())
        if (p.position > end)
            curve.add(p);

    LaneContent content{lane != nullptr ? lane->mode : AutomationMode::Read, std::move(curve)};
    return Change{SetLane{target, std::move(content)}};
}

} // namespace minidaw

#include "../exercises/ex5_check.h"
