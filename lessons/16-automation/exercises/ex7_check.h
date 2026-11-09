#pragma once
// Exercise 7 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex7_line_tool.h"

#include "MainComponent.h"

#include <dawlab/Check.h>

#include <cmath>
#include <cstdio>

namespace ex7::check {

using namespace dawlab;
using namespace dawlab::app;

inline juce::MouseEvent mouseAt(juce::Component& c, float x, float y, juce::Point<float> down, juce::ModifierKeys mods = {}) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {x, y}, mods.withFlags(juce::ModifierKeys::leftButtonModifier),
                            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, false);
}

// Press, drag, release on the tool.
inline void drawWith(LineTool& tool, float x0, float y0, float x1, float y1, juce::ModifierKeys mods = {}) {
    const juce::Point<float> down{x0, y0};
    tool.mouseDown(mouseAt(tool, x0, y0, down, mods));
    tool.mouseDrag(mouseAt(tool, (x0 + x1) / 2, (y0 + y1) / 2, down, mods));
    tool.mouseDrag(mouseAt(tool, x1, y1, down, mods));
    tool.mouseUp(mouseAt(tool, x1, y1, down, mods));
}

inline void checkCurve() {
    engine::AutomationCurve old(0.5f);
    old.add({0, 0.2f, engine::Shape::Linear});
    old.add({48000, 0.8f, engine::Shape::Hold});
    old.add({96000, 0.4f, engine::Shape::Linear});
    const engine::AutomationCurve line = drawLine(old, 24000, 0.1f, 72000, 0.9f);
    DAWLAB_CHECK(std::abs(line.valueAt(48000) - 0.5f) < 1e-6f);
    DAWLAB_CHECK(std::abs(line.valueAt(12000) - old.valueAt(12000)) < 1e-6f);
    DAWLAB_CHECK(line.valueAt(80000) == 0.8f); // the hold goes on after the line
    DAWLAB_CHECK(line.valueAt(96000) == 0.4f);
    DAWLAB_CHECK(drawLine(old, 72000, 0.9f, 24000, 0.1f) == line); // either way round
    DAWLAB_CHECK(drawLine(old, 30000, 0.9f, 30000, 0.1f) == old);  // a click draws nothing
}

inline void checkTool() {
    MainComponent main(MainComponent::Mode::Offline);
    main.setSize(MainComponent::kDefaultWidth, MainComponent::kDefaultHeight);
    DAWLAB_CHECK(main.loadDemoSongNow() == 0);
    main.simulatePlayback(512);
    Session& session = main.getSession();
    ArrangementView& view = main.getArrangement();
    view.setScrollY(0);
    const engine::TrackId track = session.getTrackId(0);
    const LaneAddress gain{StripTarget::ofTrack(track), engine::EffectId{}, "Gain"};
    view.showAutomation(track, gain);
    const juce::Rectangle<int> lane = view.getAutomationBounds(0);
    DAWLAB_CHECK(!lane.isEmpty());
    if (lane.isEmpty())
        return;

    LineTool tool(view, session);
    view.addAndMakeVisible(tool);
    tool.setBounds(view.getLocalBounds());

    // Only active, and only in the automation lanes.
    const int inLane = lane.getCentreY();
    DAWLAB_CHECK(!tool.hitTest(400, inLane));
    tool.setActive(true);
    DAWLAB_CHECK(tool.hitTest(400, inLane));
    DAWLAB_CHECK(!tool.hitTest(ArrangementView::kHeaderWidth / 2, inLane));             // the header
    DAWLAB_CHECK(!tool.hitTest(400, view.getLaneBounds(0).getY() + 5));                 // the clips
    DAWLAB_CHECK(!tool.hitTest(400, ArrangementView::kRulerHeight / 2));                // the ruler

    // A line, left to right, snapped to the beat.
    const float x0 = 300.0f, x1 = 700.0f;
    const float y0 = static_cast<float>(lane.getBottom() - 10), y1 = static_cast<float>(lane.getY() + 10);
    const std::int64_t s0 = view.snapToBeat(view.xToSample(x0), true), s1 = view.snapToBeat(view.xToSample(x1), true);
    const float v0 = view.yToValue(0, y0), v1 = view.yToValue(0, y1);
    const std::size_t entries = session.getHistory().undoCount();
    const LaneContent* before = session.getLane(gain);
    const engine::AutomationCurve oldCurve = before != nullptr ? before->curve : engine::AutomationCurve{};

    const juce::Point<float> down{x0, y0};
    tool.mouseDown(mouseAt(tool, x0, y0, down));
    tool.mouseDrag(mouseAt(tool, x1, y1, down));
    DAWLAB_CHECK(tool.isDragging() && tool.getPreview().getEnd() == juce::Point<float>(x1, y1));
    tool.mouseUp(mouseAt(tool, x1, y1, down));
    DAWLAB_CHECK(!tool.isDragging());
    const LaneContent* drawn = session.getLane(gain);
    DAWLAB_CHECK(drawn != nullptr);
    if (drawn == nullptr)
        return;
    std::printf("  a line from %lld (%.2f) to %lld (%.2f): %d points, undo \"%s\"\n", static_cast<long long>(s0), static_cast<double>(v0),
                static_cast<long long>(s1), static_cast<double>(v1), drawn->curve.size(),
                session.getHistory().getUndoName().toRawUTF8());
    DAWLAB_CHECK(drawn->curve == drawLine(oldCurve, s0, v0, s1, v1));
    DAWLAB_CHECK(std::abs(drawn->curve.valueAt((s0 + s1) / 2) - (v0 + v1) / 2) < 1e-3f);
    DAWLAB_CHECK(session.getHistory().undoCount() == entries + 1);
    DAWLAB_CHECK(session.getHistory().getUndoName() == "Draw line Gain");

    // Right to left over it: the same as drawing it the other way.
    const engine::AutomationCurve afterFirst = drawn->curve;
    drawWith(tool, 900.0f, y1, 500.0f, y0);
    const std::int64_t r0 = view.snapToBeat(view.xToSample(500.0f), true), r1 = view.snapToBeat(view.xToSample(900.0f), true);
    DAWLAB_CHECK(session.getLane(gain) != nullptr && session.getLane(gain)->curve == drawLine(afterFirst, r0, v0, r1, v1));

    // Alt: not snapped.
    const engine::AutomationCurve afterSecond = session.getLane(gain)->curve;
    drawWith(tool, 333.0f, y0, 444.0f, y0, juce::ModifierKeys::altModifier);
    const std::int64_t a0 = view.snapToBeat(view.xToSample(333.0f), false), a1 = view.snapToBeat(view.xToSample(444.0f), false);
    DAWLAB_CHECK(session.getLane(gain) != nullptr && session.getLane(gain)->curve == drawLine(afterSecond, a0, v0, a1, v0));

    // Dragged out of the lane: the end stays on the lane's edge.
    const engine::AutomationCurve afterThird = session.getLane(gain)->curve;
    {
        const juce::Point<float> from{300.0f, y0};
        tool.mouseDown(mouseAt(tool, 300.0f, y0, from));
        tool.mouseDrag(mouseAt(tool, 600.0f, static_cast<float>(lane.getY() - 200), from));
        DAWLAB_CHECK(tool.getPreview().getEndY() == static_cast<float>(lane.getY())); // the preview stays in the lane
        tool.mouseUp(mouseAt(tool, 600.0f, static_cast<float>(lane.getY() - 200), from));
    }
    const std::int64_t o0 = view.snapToBeat(view.xToSample(300.0f), true), o1 = view.snapToBeat(view.xToSample(600.0f), true);
    DAWLAB_CHECK(session.getLane(gain) != nullptr &&
                 session.getLane(gain)->curve == drawLine(afterThird, o0, v0, o1, view.yToValue(0, static_cast<float>(lane.getY()))));

    // A click draws nothing and leaves no undo step.
    const std::size_t now = session.getHistory().undoCount();
    drawWith(tool, 450.0f, y0, 450.0f, y0);
    DAWLAB_CHECK(session.getHistory().undoCount() == now);
    DAWLAB_CHECK(now == entries + 4);

    // Every line is one undo step; all undone, the lane is as it was.
    for (int i = 0; i < 4; ++i)
        DAWLAB_CHECK(main.undo());
    const LaneContent* undone = session.getLane(gain);
    DAWLAB_CHECK((undone == nullptr && before == nullptr) || (undone != nullptr && undone->curve == oldCurve));
}

} // namespace ex7::check

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf("Exercise 7 - a line tool for automation lanes\n");
    ex7::check::checkCurve();
    ex7::check::checkTool();
    return dawlab::test::summary("l16_ex7");
}
