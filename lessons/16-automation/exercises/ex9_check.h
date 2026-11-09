#pragma once
// Exercise 9 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex9_lane_menu.h"

#include "MainComponent.h"

#include <dawlab/Check.h>

#include <cmath>
#include <cstdio>

namespace ex9::check {

using namespace dawlab;
using namespace dawlab::app;
using engine::AutomationCurve;
using engine::Shape;

// A wobble drawn point by point, a hold, a jump, and a straight ramp of many points.
inline AutomationCurve busyCurve() {
    AutomationCurve c(0.5f);
    for (int k = 0; k <= 100; ++k)
        c.add({k * 480, 0.5f + 0.3f * static_cast<float>(std::sin(k * 0.12)), Shape::Linear});
    c.add({30240, c.valueAt(30240) + 0.001f, Shape::Linear}); // a jump smaller than the tolerance
    c.add({48480, 0.7f, Shape::Hold});
    c.add({60000, 0.7f + 0.2f * 11520.0f / 23520.0f, Shape::Linear}); // on the line from the hold to the jump
    c.add({72000, 0.9f, Shape::Linear});
    c.add({72000, 0.1f, Shape::Linear}); // a jump
    for (int k = 1; k <= 48; ++k)
        c.add({72000 + k * 500, 0.1f + 0.5f * static_cast<float>(k) / 48.0f, Shape::Linear});
    return c;
}

inline const juce::PopupMenu::Item* findItem(const juce::PopupMenu& menu, const juce::String& prefix, juce::PopupMenu::Item& out) {
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (it.getItem().text.startsWith(prefix)) {
            out = it.getItem();
            return &out;
        }
    return nullptr;
}

inline void checkCurves() {
    const AutomationCurve c = busyCurve();
    const AutomationCurve t = thinCurve(c, kThinTolerance);
    double worst = 0.0;
    for (std::int64_t p = -10; p < 100000; p += 3)
        worst = std::max(worst, std::abs(static_cast<double>(t.valueAt(p)) - c.valueAt(p)));
    int inRamp = 0;
    for (const engine::AutomationPoint& p : t.points())
        inRamp += p.position > 72000 && p.position < 96000 ? 1 : 0;
    std::printf("  thinned %d points to %d, off by at most %.5f (tolerance %.3f)\n", c.size(), t.size(), worst,
                static_cast<double>(kThinTolerance));
    DAWLAB_CHECK(t.size() < c.size() / 2);
    DAWLAB_CHECK(worst <= kThinTolerance + 1e-6);
    DAWLAB_CHECK(inRamp == 0);                          // a straight ramp needs only its ends
    DAWLAB_CHECK(t.valueAt(50000) == 0.7f && t.valueAt(59999) == 0.7f); // the hold, exactly
    DAWLAB_CHECK(t.valueAt(60000) == c.valueAt(60000)); // the point after a hold stays, even on a line
    int atSmallJump = 0;
    for (const engine::AutomationPoint& p : t.points())
        atSmallJump += p.position == 30240 ? 1 : 0;
    DAWLAB_CHECK(atSmallJump == 2); // a jump is a jump, however small
    DAWLAB_CHECK(t.valueAt(72000) == 0.1f && std::abs(t.valueAt(71999) - c.valueAt(71999)) < 1e-6f); // the jump
    DAWLAB_CHECK(t.points().front() == c.points().front() && t.points().back() == c.points().back());
    DAWLAB_CHECK(thinCurve(t, kThinTolerance) == t); // thinning twice changes nothing

    const AutomationCurve cleared = clearRange(c, 10000, 40000);
    double outside = 0.0, inside = 0.0;
    int left = 0;
    for (const engine::AutomationPoint& p : cleared.points())
        left += p.position > 10000 && p.position < 40000 ? 1 : 0;
    const float a = c.valueAt(10000), b = valueBefore(c, 40000);
    for (std::int64_t p = 0; p < 100000; p += 7) {
        if (p >= 10000 && p < 40000)
            inside = std::max(inside, std::abs(static_cast<double>(cleared.valueAt(p)) - (a + (b - a) * (p - 10000) / 30000.0)));
        else
            outside = std::max(outside, std::abs(static_cast<double>(cleared.valueAt(p)) - c.valueAt(p)));
    }
    DAWLAB_CHECK(left == 0 && inside < 1e-6 && outside < 1e-6);
    DAWLAB_CHECK(clearRange(c, 5000, 5000) == c);
    // Ending on the jump: the line goes to the value BEFORE it, and the jump stays.
    const AutomationCurve toJump = clearRange(c, 60000, 72000);
    DAWLAB_CHECK(std::abs(toJump.valueAt(71999) - c.valueAt(71999)) < 1e-4f && toJump.valueAt(72000) == 0.1f);
}

inline void checkMenu() {
    MainComponent main(MainComponent::Mode::Offline);
    DAWLAB_CHECK(main.loadDemoSongNow() == 0);
    main.simulatePlayback(512);
    Session& session = main.getSession();
    const LaneAddress gain{StripTarget::ofTrack(session.getTrackId(0)), engine::EffectId{}, "Gain"};
    const LaneAddress empty{StripTarget::ofTrack(session.getTrackId(1)), engine::EffectId{}, "Gain"};
    DAWLAB_CHECK(session.setLane(gain, LaneContent{engine::AutomationMode::Read, busyCurve()}));
    engine::Transport& tr = session.getEngine().getTransport();
    tr.setLoopRange(10000, 40000);
    const std::size_t entries = session.getHistory().undoCount();
    juce::PopupMenu::Item item;

    // Thin: one step, undone exactly.
    juce::PopupMenu menu;
    addLaneItems(menu, session, gain);
    DAWLAB_CHECK(findItem(menu, "Thin Points", item) != nullptr && item.isEnabled && item.action);
    if (item.action) {
        std::printf("  menu: \"%s\"\n", item.text.toRawUTF8());
        item.action();
        DAWLAB_CHECK(session.getHistory().undoCount() == entries + 1 && session.getHistory().getUndoName() == "Thin Gain");
        DAWLAB_CHECK(session.getLane(gain) != nullptr && session.getLane(gain)->curve == thinCurve(busyCurve(), kThinTolerance));
        juce::PopupMenu twice;
        addLaneItems(twice, session, gain);
        DAWLAB_CHECK(findItem(twice, "Thin Points", item) != nullptr && !item.isEnabled); // nothing left to thin
        DAWLAB_CHECK(main.undo() && session.getLane(gain)->curve == busyCurve());
    }

    // Clear between the locators.
    item = {};
    juce::PopupMenu menu2;
    addLaneItems(menu2, session, gain);
    DAWLAB_CHECK(findItem(menu2, "Clear Between Locators", item) != nullptr && item.isEnabled && item.action);
    if (item.action) {
        item.action();
        DAWLAB_CHECK(session.getHistory().getUndoName() == "Clear Gain");
        DAWLAB_CHECK(session.getLane(gain) != nullptr && session.getLane(gain)->curve == clearRange(busyCurve(), 10000, 40000));
        DAWLAB_CHECK(main.undo() && session.getLane(gain)->curve == busyCurve());
    }
    tr.setLoopRange(200000, 300000); // no point there
    juce::PopupMenu menu3;
    addLaneItems(menu3, session, gain);
    DAWLAB_CHECK(findItem(menu3, "Clear Between Locators", item) != nullptr && !item.isEnabled);

    // Remove, and back.
    DAWLAB_CHECK(findItem(menu3, "Remove Lane", item) != nullptr && item.isEnabled && item.action);
    if (item.action) {
        item.action();
        DAWLAB_CHECK(session.getLane(gain) == nullptr && session.getHistory().getUndoName() == "Remove lane Gain");
        DAWLAB_CHECK(main.undo() && session.getLane(gain) != nullptr && session.getLane(gain)->curve == busyCurve());
    }

    // No lane: nothing to do.
    juce::PopupMenu none;
    addLaneItems(none, session, empty);
    int items = 0, enabled = 0;
    for (juce::PopupMenu::MenuItemIterator it(none); it.next();) {
        ++items;
        enabled += it.getItem().isEnabled ? 1 : 0;
    }
    DAWLAB_CHECK(items == 3 && enabled == 0);
    DAWLAB_CHECK(session.getHistory().undoCount() == entries);
}

} // namespace ex9::check

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf("Exercise 9 - a lane menu: thin, clear, remove\n");
    ex9::check::checkCurves();
    ex9::check::checkMenu();
    return dawlab::test::summary("l16_ex9");
}
