// Exercise 7 (solution): a line tool for automation lanes.
//
// Build and run:  l16_ex7_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex7_line_tool.cpp -> l16_ex7_starter

#include "../exercises/ex7_line_tool.h"

#include <utility>

using namespace dawlab;
using namespace dawlab::app;

namespace ex7 {

engine::AutomationCurve drawLine(const engine::AutomationCurve& old, std::int64_t x0, float v0, std::int64_t x1, float v1) {
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(v0, v1);
    }
    if (x0 == x1)
        return old;
    // Step 3's punch() replaces a range with a pass and keeps the curve outside, with jumps at the ends:
    // a line is a pass of two points.
    return engine::punch(old, {{x0, v0, engine::Shape::Linear}, {x1, v1, engine::Shape::Linear}});
}

bool drawLineInLane(Session& session, const LaneAddress& lane, std::int64_t x0, float v0, std::int64_t x1, float v1) {
    if (x0 == x1)
        return false;
    const LaneContent* old = session.getLane(lane);
    LaneContent content = old != nullptr ? *old : LaneContent{};
    content.curve = drawLine(content.curve, std::max<std::int64_t>(0, x0), v0, std::max<std::int64_t>(0, x1), v1);
    return session.perform(edit::SetLane{lane, std::move(content)}, "Draw line " + lane.parameter);
}

bool LineTool::hitTest(int x, int y) {
    return active_ && x >= ArrangementView::kHeaderWidth && arrangement_.automationLaneAt(y) >= 0;
}

void LineTool::mouseDown(const juce::MouseEvent& e) {
    dragLane_ = arrangement_.automationLaneAt(e.y);
    if (dragLane_ < 0)
        return;
    start_ = end_ = e.position;
    repaint();
}

void LineTool::mouseDrag(const juce::MouseEvent& e) {
    if (dragLane_ < 0)
        return;
    // Keep the end inside the lane it started in, whatever row the mouse is over now.
    const juce::Rectangle<int> lane = arrangement_.getAutomationBounds(dragLane_);
    end_ = {e.position.x, juce::jlimit(static_cast<float>(lane.getY()), static_cast<float>(lane.getBottom()), e.position.y)};
    repaint();
}

void LineTool::mouseUp(const juce::MouseEvent& e) {
    mouseDrag(e);
    const int lane = dragLane_;
    dragLane_ = -1;
    repaint();
    if (lane < 0)
        return;
    const engine::TrackId track = session_.getTrackId(lane);
    const LaneAddress* shown = arrangement_.getShownLane(track);
    if (shown == nullptr)
        return;
    const bool snap = !e.mods.isAltDown();
    const std::int64_t x0 = arrangement_.snapToBeat(arrangement_.xToSample(start_.x), snap);
    const std::int64_t x1 = arrangement_.snapToBeat(arrangement_.xToSample(end_.x), snap);
    drawLineInLane(session_, *shown, x0, arrangement_.yToValue(lane, start_.y), x1, arrangement_.yToValue(lane, end_.y));
}

} // namespace ex7

#include "../exercises/ex7_check.h"
