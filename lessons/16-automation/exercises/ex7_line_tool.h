#pragma once
// Exercise 7 - A line tool for automation lanes (optional, JUCE).
//
// DawLab 16 draws automation point by point. A fade over eight bars wants one gesture: press where it
// starts, release where it ends, and a straight line replaces what was in between. Without changing
// the app:
//
//   drawLine(old, x0, v0, x1, v1)       the curve with a straight line from (x0, v0) to (x1, v1) in
//                                       place of everything between them; outside, the old curve
//   drawLineInLane(session, lane, ...)  the same as one undo step "Draw line <parameter>"
//   LineTool                            a transparent component over the ArrangementView. While it is
//                                       active it takes the mouse in the automation lanes - and only
//                                       there: hitTest() says no everywhere else, so clips, headers
//                                       and the ruler still get their clicks. Positions snap to the
//                                       beat like the arrangement's own points (Alt = free).

#include "ArrangementView.h"
#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ex7 {

// ---- YOUR TURN (starter/ex7_line_tool.cpp) --------------------------------------------------------
// Swaps the ends if x1 < x0. Two points at one position draw nothing: the old curve comes back.
dawlab::engine::AutomationCurve drawLine(const dawlab::engine::AutomationCurve& old, std::int64_t x0, float v0, std::int64_t x1,
                                         float v1);

// One undo step. A lane that does not exist yet starts as the Session's do: LaneContent{} (Read, 0).
// False if nothing was drawn (one position) or the edit did not apply.
bool drawLineInLane(dawlab::app::Session& session, const dawlab::app::LaneAddress& lane, std::int64_t x0, float v0, std::int64_t x1,
                    float v1);

class LineTool final : public juce::Component {
public:
    LineTool(dawlab::app::ArrangementView& arrangement, dawlab::app::Session& session) : arrangement_(arrangement), session_(session) {
        setInterceptsMouseClicks(true, false);
    }

    void setActive(bool active) {
        active_ = active;
        dragLane_ = -1;
        repaint();
    }
    bool isActive() const noexcept { return active_; }

    // YOUR TURN: active, right of the header column, in an automation lane.
    bool hitTest(int x, int y) override;
    // YOUR TURN: press = the start (lane, snapped position, value); drag = the end so far; release =
    // drawLineInLane() with the lane shown in that track (ArrangementView::getShownLane).
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // While dragging: the line so far, in the arrangement's coordinates (for paint() and the checks).
    bool isDragging() const noexcept { return dragLane_ >= 0; }
    juce::Line<float> getPreview() const noexcept { return {start_, end_}; }

    void paint(juce::Graphics& g) override {
        if (isDragging()) {
            g.setColour(juce::Colours::orange);
            g.drawLine(getPreview(), 2.0f);
        }
    }

private:
    dawlab::app::ArrangementView& arrangement_;
    dawlab::app::Session& session_;
    bool active_ = false;
    int dragLane_ = -1;
    juce::Point<float> start_, end_;
};

} // namespace ex7
