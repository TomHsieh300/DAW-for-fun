#pragma once
// TempoLane: the tempo map, drawn and edited above the arrangement (chapter 19, Step 5).
//
// A strip that shares the arrangement's time axis. It draws the map - a level line for a tempo that holds, a
// sloped one for a ramp, a dot on every point - and edits it:
//
//   double click on nothing   a new point on the nearest beat, at the tempo of the click's height
//   drag a point              moves it: in time to the nearest beat (Shift: any tick), between its neighbours;
//                             in tempo to a whole BPM (Shift: any). The first point stays at tick 0.
//   Alt + click a point       its tempo ramps to the next point's, or holds again
//   right click a point       deletes it (not the first)
//
// Each gesture is one undo step (an edit::SetTempoMap; every step of a drag carries the same merge key).
//
// The time axis is in SAMPLES, so moving a tempo point moves every beat after it on the screen - also the point
// under the mouse. A drag therefore reads the mouse through the map as it was when the drag began: the point goes
// to the beat the mouse was over then, and does not run away from it.
//
// As in the piano roll, the mouse handlers only call press(), dragTo() and release(), and so can a test.

#include "ArrangementView.h"
#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace dawlab::app {

class TempoLane final : public juce::Component, private juce::ChangeListener {
public:
    static constexpr int kHeight = 64;
    static constexpr double kLowBpm = 40.0, kHighBpm = 240.0; // what the strip's height shows

    TempoLane(Session& session, ArrangementView& arrangement);
    ~TempoLane() override;

    float bpmToY(double bpm) const noexcept;
    double yToBpm(float y) const noexcept;
    float tickToX(int64_t tick) const noexcept;
    // The point (its index in the map) under p, or -1.
    int pointAt(juce::Point<float> p) const;
    juce::Point<float> getPointPosition(int index) const;

    // ---- gestures: the mouse, or a test -------------------------------------------------------------------
    void press(juce::Point<float> p, juce::ModifierKeys mods, int clicks = 1);
    void dragTo(juce::Point<float> p, juce::ModifierKeys mods);
    void release();
    // Asked at press() and at every step of a drag: true when no tempo change may be made now (MainComponent:
    // while a recording runs). It says why itself.
    std::function<bool()> refuseEdit;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent& e) override { press(e.position, e.mods, e.getNumberOfClicks()); }
    void mouseDrag(const juce::MouseEvent& e) override { dragTo(e.position, e.mods); }
    void mouseUp(const juce::MouseEvent&) override { release(); }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }
    // The tick at x through `map` (the session's now, or a drag's).
    int64_t xToTick(float x, const engine::TempoMap& map, bool snapToBeat) const noexcept;

    Session& session_;
    ArrangementView& arrangement_;
    int dragPoint_ = -1;              // the point being dragged
    std::uint32_t dragKey_ = 0;       // its merge key
    engine::TempoMap dragMap_;        // the map when the drag began
};

} // namespace dawlab::app
