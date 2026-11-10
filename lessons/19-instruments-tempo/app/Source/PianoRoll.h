#pragma once
// PianoRoll: the notes of one MIDI clip, to draw and edit (chapter 18, Step 8).
//
//   +------+---1-------2-------3-------4---- ruler: beats of the clip's material
//   | C4 # |   [====]      [==]               one row per pitch, C rows marked
//   | B3   |        [======]                  a note: a bar from its start to its end
//   | ...  |
//
// Coordinates: x is a tick of the clip's material (Note::start, from the clip's own zero), y a pitch:
//
//   x(t) = keyWidth + (t - viewTick) / ticksPerPixel        y(p) = (127 - p) * rowHeight - scrollY
//
// Every gesture is one undo step (Session::replaceMidiClip with one merge key):
//
//   on nothing     add a note on the grid, one grid long; dragging right makes it longer
//   on a note      drag: move it, in time (on the grid; Alt = free) and in pitch
//   on its end     drag: change its length (at least one tick)
//   Alt + drag up/down on a note   its velocity
//   right click or double click on a note   delete it
//
// The roll edits the notes AS PLAYED. With quantize on, they play on the grid; the roll draws where they play
// as a thin outline, so both are visible.
//
// The mouse handlers only translate the event into press / dragTo / release, which the smoke test calls as
// well: the roll is tested without a mouse.

#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace dawlab::app {

class PianoRoll final : public juce::Component, private juce::ChangeListener {
public:
    static constexpr int kKeyWidth = 44;
    static constexpr int kRulerHeight = 20;
    static constexpr int kRowHeight = 10;

    PianoRoll(Session& session, engine::TrackId track, int clipId);
    ~PianoRoll() override;

    // ---- what it edits ---------------------------------------------------------------------------
    engine::TrackId getTrack() const noexcept { return track_; }
    int getClipId() const noexcept { return clipId_; }
    // The clip now, or nullptr when it is gone (deleted, or its track).
    const MidiClipModel* getClip() const noexcept;

    // ---- view ----------------------------------------------------------------------------------
    std::int64_t getGrid() const noexcept { return grid_; }
    void setGrid(std::int64_t ticks) noexcept { grid_ = std::max<std::int64_t>(1, ticks); }
    double getTicksPerPixel() const noexcept { return ticksPerPixel_; }
    void setView(double viewTick, double ticksPerPixel);
    void scrollToPitch(int pitch); // centred
    float tickToX(double tick) const noexcept;
    double xToTick(float x) const noexcept;
    float pitchToY(int pitch) const noexcept;
    int yToPitch(float y) const noexcept;
    // The note under a point (topmost), -1 if none. `onEnd`: the point is on its last few pixels.
    int noteAt(juce::Point<float> p, bool* onEnd = nullptr) const;
    juce::Rectangle<float> getNoteBounds(int index) const;

    // ---- gestures: the mouse, or a test ----------------------------------------------------------------
    void press(juce::Point<float> p, juce::ModifierKeys mods, int clicks = 1);
    void dragTo(juce::Point<float> p, juce::ModifierKeys mods);
    void release();

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent& e) override { press(e.position, e.mods, e.getNumberOfClicks()); }
    void mouseDrag(const juce::MouseEvent& e) override { dragTo(e.position, e.mods); }
    void mouseUp(const juce::MouseEvent&) override { release(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Gesture { None, Draw, Move, Resize, Velocity };
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }
    int clipIndex() const noexcept;
    // Applies `notes` as one step of the current gesture.
    void apply(std::vector<engine::Note> notes, const juce::String& name);
    std::int64_t snap(double tick, bool free) const noexcept;

    Session& session_;
    engine::TrackId track_;
    int clipId_;
    std::int64_t grid_ = 240; // a sixteenth
    double viewTick_ = 0.0;
    double ticksPerPixel_ = 4.0;
    int scrollY_ = 0;

    Gesture gesture_ = Gesture::None;
    std::uint32_t mergeKey_ = 0;
    engine::Note grabbed_;      // the note as it was when the gesture started
    int grabbedIndex_ = -1;     // its index in the played notes, kept up to date
    juce::Point<float> start_;  // where the gesture started
    double grabOffset_ = 0.0;   // ticks between the grab and the note's start

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRoll)
};

class PianoRollWindow final : public juce::DocumentWindow {
public:
    PianoRollWindow(Session& session, engine::TrackId track, int clipId, const juce::String& title, juce::LookAndFeel* lookAndFeel);
    ~PianoRollWindow() override;
    PianoRoll& getRoll() noexcept { return *roll_; }
    std::function<void()> onClose;
    void closeButtonPressed() override;

private:
    PianoRoll* roll_ = nullptr; // owned by the window (setContentOwned)
};

} // namespace dawlab::app
