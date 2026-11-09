// Exercise 7 (starter): a line tool for automation lanes.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex7_line_tool.cpp. What each function must do is in ../ex7_line_tool.h.
// Build with -Juce: .\scripts\build.ps1 -Only 16-automation -Juce

#include "../ex7_line_tool.h"

#include <utility>

using namespace dawlab;
using namespace dawlab::app;

namespace ex7 {

engine::AutomationCurve drawLine(const engine::AutomationCurve& old, std::int64_t x0, float v0, std::int64_t x1, float v1) {
    // TODO: swap the ends if needed; one position -> old. Hint: a line is a pass of two points, and
    //       engine::punch() (Step 3) puts a pass in place of a range.
    (void)x0;
    (void)v0;
    (void)x1;
    (void)v1;
    return old;
}

bool drawLineInLane(Session& session, const LaneAddress& lane, std::int64_t x0, float v0, std::int64_t x1, float v1) {
    // TODO: the lane's content (or LaneContent{}), its curve through drawLine(), then
    //       session.perform(edit::SetLane{...}, "Draw line " + lane.parameter).
    (void)session;
    (void)lane;
    (void)x0;
    (void)v0;
    (void)x1;
    (void)v1;
    return false;
}

bool LineTool::hitTest(int x, int y) {
    // TODO: active_, x >= ArrangementView::kHeaderWidth, arrangement_.automationLaneAt(y) >= 0.
    (void)x;
    (void)y;
    return false;
}

void LineTool::mouseDown(const juce::MouseEvent& e) {
    // TODO: dragLane_ = the automation lane under the mouse; start_ = end_ = e.position.
    (void)e;
}

void LineTool::mouseDrag(const juce::MouseEvent& e) {
    // TODO: end_ = e.position, its y kept inside the lane's getAutomationBounds().
    (void)e;
}

void LineTool::mouseUp(const juce::MouseEvent& e) {
    // TODO: the last drag; then drawLineInLane() into the lane shown for that track, with the
    //       positions snapToBeat(xToSample(x), !Alt) and the values yToValue(lane, y). dragLane_ = -1.
    (void)e;
}

} // namespace ex7

#include "../ex7_check.h"
