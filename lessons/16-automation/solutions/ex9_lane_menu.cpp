// Exercise 9 (solution): a lane menu - thin, clear, remove.
//
// Build and run:  l16_ex9_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex9_lane_menu.cpp -> l16_ex9_starter

#include "../exercises/ex9_lane_menu.h"

#include <vector>

using namespace dawlab;
using namespace dawlab::app;

namespace ex9 {

engine::AutomationCurve thinCurve(const engine::AutomationCurve& curve, float tolerance) {
    const std::vector<engine::AutomationPoint>& p = curve.points();
    const size_t n = p.size();
    engine::AutomationCurve result(curve.defaultValue());
    if (n <= 2) {
        for (const engine::AutomationPoint& point : p)
            result.add(point);
        return result;
    }
    auto anchor = [&p, n](size_t i) {
        return i == 0 || i == n - 1 || p[i].shape == engine::Shape::Hold || p[i - 1].shape == engine::Shape::Hold ||
               p[i].position == p[i - 1].position || p[i + 1].position == p[i].position;
    };
    // Points are added in their order, so the two points of a jump stay in theirs.
    result.add(p[0]);
    size_t from = 0;
    for (size_t i = 1; i < n; ++i) {
        if (!anchor(i))
            continue;
        // p[from .. i]: anchors at both ends, a plain line run in between.
        const std::vector<engine::AutomationPoint> run(p.begin() + static_cast<std::ptrdiff_t>(from),
                                                       p.begin() + static_cast<std::ptrdiff_t>(i) + 1);
        const std::vector<engine::AutomationPoint> kept = run.size() > 2 ? engine::thin(run, tolerance) : run;
        for (size_t k = 1; k < kept.size(); ++k) // the first one is already in
            result.add(kept[k]);
        from = i;
    }
    return result;
}

engine::AutomationCurve clearRange(const engine::AutomationCurve& curve, std::int64_t start, std::int64_t end) {
    if (end <= start)
        return curve;
    return engine::punch(curve, {{start, curve.valueAt(start), engine::Shape::Linear}, {end, valueBefore(curve, end), engine::Shape::Linear}});
}

void addLaneItems(juce::PopupMenu& menu, Session& session, const LaneAddress& lane) {
    const LaneContent* content = session.getLane(lane);
    Session* s = &session;

    const engine::AutomationCurve thinned = content != nullptr ? thinCurve(content->curve, kThinTolerance) : engine::AutomationCurve{};
    const bool canThin = content != nullptr && thinned.size() < content->curve.size();
    juce::String thinText = "Thin Points";
    if (canThin)
        thinText << " (" << content->curve.size() << " -> " << thinned.size() << ")";
    menu.addItem(juce::PopupMenu::Item(thinText).setEnabled(canThin).setAction([s, lane, thinned] {
        LaneContent next = *s->getLane(lane);
        next.curve = thinned;
        s->perform(edit::SetLane{lane, std::move(next)}, "Thin " + lane.parameter);
    }));

    const engine::Transport& tr = session.getEngine().getTransport();
    const std::int64_t from = tr.getLoopStart(), to = tr.getLoopEnd();
    bool inside = false;
    if (content != nullptr && to > from)
        for (const engine::AutomationPoint& p : content->curve.points())
            inside = inside || (p.position > from && p.position < to);
    menu.addItem(juce::PopupMenu::Item("Clear Between Locators").setEnabled(inside).setAction([s, lane, from, to] {
        LaneContent next = *s->getLane(lane);
        next.curve = clearRange(next.curve, from, to);
        s->perform(edit::SetLane{lane, std::move(next)}, "Clear " + lane.parameter);
    }));

    menu.addItem(juce::PopupMenu::Item("Remove Lane").setEnabled(content != nullptr).setAction([s, lane] {
        s->perform(edit::SetLane{lane, std::nullopt}, "Remove lane " + lane.parameter);
    }));
}

} // namespace ex9

#include "../exercises/ex9_check.h"
