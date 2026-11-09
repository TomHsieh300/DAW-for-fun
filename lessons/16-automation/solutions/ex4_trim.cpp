// Exercise 4 (solution): trim - move a curve up and down instead of replacing it.
//
// Build and run:  l16_ex4_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex4_trim.cpp -> l16_ex4_starter

#include "../exercises/ex4_trim.h"

namespace minidaw {

AutomationCurve applyTrim(const AutomationCurve& old, const std::vector<TrimPoint>& pass) {
    const std::int64_t first = pass.front().position, last = pass.back().position;

    // Where the sum can bend: every point of either curve in [first, last].
    std::vector<std::int64_t> breaks;
    for (const TrimPoint& t : pass)
        breaks.push_back(t.position);
    for (const AutomationPoint& p : old.points())
        if (p.position > first && p.position < last)
            breaks.push_back(p.position);
    std::sort(breaks.begin(), breaks.end());
    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());

    AutomationCurve curve(old.defaultValue());
    for (const AutomationPoint& p : old.points())
        if (p.position < first)
            curve.add(p);
    curve.add({first, valueBefore(old, first), Shape::Linear});
    for (const std::int64_t x : breaks) {
        const float offset = offsetAt(pass, x);
        const float before = valueBefore(old, x), after = old.valueAt(x);
        // add() clamps to 0..1, and keeps two points at x in the order they are added: a jump.
        if (before != after && x != first)
            curve.add({x, before + offset, Shape::Linear});
        curve.add({x, after + offset, Shape::Linear});
    }
    curve.add({last, old.valueAt(last), shapeAt(old, last)});
    for (const AutomationPoint& p : old.points())
        if (p.position > last)
            curve.add(p);
    return curve;
}

std::optional<Change> trimChange(const Project& project, const AutomationTarget& target, const std::vector<TrimPoint>& pass) {
    const AutomationLaneState* lane = findLane(project, target);
    if (lane == nullptr || pass.size() < 2)
        return std::nullopt;
    return Change{SetLane{target, LaneContent{lane->mode, applyTrim(lane->curve, pass)}}};
}

} // namespace minidaw

#include "../exercises/ex4_check.h"
