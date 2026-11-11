#pragma once
// SynthPanel: the built-in synth's sound for one track (chapter 19, Step 6; chapter 18's Exercise 8 in the app).
//
// Seven sliders - cutoff, resonance, attack, decay, sustain, release, level. Moving one changes the sound at once,
// while the synth plays, and is undoable: every step of one drag is the same undo step (an
// edit::SetInstrumentSound with one merge key), and the project file keeps the settings.
//
// The panel shows what the session has: after an undo, or when another track is shown, the sliders follow -
// without sending anything back. A track that is not an instrument, has a plugin in the synth's place, or is gone
// turns the sliders off and says why.
//
// As in the piano roll, a test moves it without a mouse: beginGesture(), setKnob(), endGesture().

#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace dawlab::app {

class SynthPanel final : public juce::Component, private juce::ChangeListener {
public:
    enum Knob { Cutoff, Resonance, Attack, Decay, Sustain, Release, Level, kNumKnobs };
    struct KnobInfo {
        const char* label;
        double min, max;
        bool logarithmic;
    };
    static const KnobInfo& infoOf(Knob knob) noexcept;
    static double valueOf(const engine::VoiceSettings& s, Knob knob) noexcept;
    static void setValueOf(engine::VoiceSettings& s, Knob knob, double value) noexcept;

    explicit SynthPanel(Session& session);
    ~SynthPanel() override;

    void showTrack(engine::TrackId track);
    engine::TrackId getTrack() const noexcept { return track_; }
    bool isEnabledForTrack() const noexcept { return enabled_; }
    juce::String getMessage() const { return message_.getText(); }
    double getKnob(Knob knob) const { return sliders_[static_cast<size_t>(knob)].getValue(); }

    // ---- gestures: a slider, or a test ---------------------------------------------------------------------
    void beginGesture();
    void setKnob(Knob knob, double value); // clamped to the knob's range; nothing if the track cannot take it
    void endGesture();

    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    // The sliders from the session, without notifications.
    void refresh();

    Session& session_;
    engine::TrackId track_;
    bool enabled_ = false;
    std::uint32_t gestureKey_ = 0;
    std::array<juce::Slider, kNumKnobs> sliders_;
    std::array<juce::Label, kNumKnobs> labels_;
    juce::Label message_;
};

class SynthPanelWindow final : public juce::DocumentWindow {
public:
    SynthPanelWindow(Session& session, juce::LookAndFeel* lookAndFeel);
    ~SynthPanelWindow() override;
    SynthPanel& getPanel() noexcept { return *panel_; }
    void closeButtonPressed() override { setVisible(false); }

private:
    SynthPanel* panel_ = nullptr; // owned by the window (setContentOwned)
};

} // namespace dawlab::app
