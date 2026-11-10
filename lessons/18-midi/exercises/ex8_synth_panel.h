#pragma once
// Exercise 8 - A synth settings panel (optional, JUCE).
//
// DawLab 18's synth had one sound. The engine now lets its settings change while it plays (Step 7, "Added after
// the chapter"): Engine::getTrackSynth(track) gives a track's SynthInstrument, and its setSettings() may be called
// on the UI thread at any time - the next block hands the values to the voices, without a click and without
// allocating. This exercise is the panel in front of it, without changing the app:
//
//   SynthPanel     seven sliders (cutoff, resonance, attack, decay, sustain, release, level) for one track.
//                  showTrack() shows a track's settings; moving a slider - by hand or from a check - sends the
//                  whole settings to that track's synth at once. A track that is not an instrument, or is gone,
//                  disables the sliders and says why.
//   toText()       the settings as one line of text, "cutoff=4000 resonance=0.9 attack=0.005 decay=0.2
//   fromText()     sustain=0.6 release=0.3 level=0.25": what a project file would keep, or a preset. Every key
//                  once, in any order, each value inside its slider's range; anything else is refused.
//
// The panel finds the synth by the track's id every time: a pointer kept from showTrack() would outlive a deleted
// track, and belong to the next track in its slot.

#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace ex8 {

using dawlab::engine::TrackId;
using dawlab::engine::VoiceSettings;

enum class Knob { Cutoff, Resonance, Attack, Decay, Sustain, Release, Level };
constexpr int kNumKnobs = 7;

struct KnobInfo {
    const char* key; // in the text form
    const char* label;
    double min, max;
    bool logarithmic;
};

// ---- provided -------------------------------------------------------------------------------------------------
inline const KnobInfo& infoOf(Knob knob) noexcept {
    static const std::array<KnobInfo, kNumKnobs> table{{
        {"cutoff", "Cutoff (Hz)", 50.0, 16000.0, true},
        {"resonance", "Resonance", 0.5, 10.0, false},
        {"attack", "Attack (s)", 0.001, 2.0, true},
        {"decay", "Decay (s)", 0.001, 5.0, true},
        {"sustain", "Sustain", 0.0, 1.0, false},
        {"release", "Release (s)", 0.001, 5.0, true},
        {"level", "Level", 0.0, 1.0, false},
    }};
    return table[static_cast<size_t>(knob)];
}
inline double valueOf(const VoiceSettings& s, Knob knob) noexcept {
    switch (knob) {
    case Knob::Cutoff: return s.cutoffHz;
    case Knob::Resonance: return s.resonance;
    case Knob::Attack: return s.attack;
    case Knob::Decay: return s.decay;
    case Knob::Sustain: return s.sustain;
    case Knob::Release: return s.release;
    case Knob::Level: return s.level;
    }
    return 0.0;
}
inline void setValueOf(VoiceSettings& s, Knob knob, double v) noexcept {
    switch (knob) {
    case Knob::Cutoff: s.cutoffHz = v; break;
    case Knob::Resonance: s.resonance = v; break;
    case Knob::Attack: s.attack = v; break;
    case Knob::Decay: s.decay = v; break;
    case Knob::Sustain: s.sustain = v; break;
    case Knob::Release: s.release = v; break;
    case Knob::Level: s.level = static_cast<float>(v); break;
    }
}

// ---- YOUR TURN (starter/ex8_synth_panel.cpp) --------------------------------------------------------------------
juce::String toText(const VoiceSettings& settings);
bool fromText(const juce::String& text, VoiceSettings& out); // `out` changes only when the text is good

class SynthPanel final : public juce::Component {
public:
    explicit SynthPanel(dawlab::app::MainComponent& main);

    // ---- YOUR TURN ----------------------------------------------------------------------------------------
    // The sliders show the track's settings, and nothing is sent to the synth for it. Not an instrument, or no
    // such track: the sliders are disabled, and getMessage() says which.
    void showTrack(TrackId track);

    // ---- provided -----------------------------------------------------------------------------------------
    TrackId getTrack() const noexcept { return track_; }
    juce::Slider& slider(Knob knob) noexcept { return sliders_[static_cast<size_t>(knob)]; }
    juce::String getMessage() const { return message_.getText(); }
    void resized() override;

private:
    // ---- YOUR TURN ----------------------------------------------------------------------------------------
    // A slider moved: the settings of all seven sliders to the track's synth - if the track is still here and an
    // instrument; otherwise disable the sliders and say so.
    void sliderMoved();

    dawlab::app::MainComponent& main_;
    TrackId track_;
    std::array<juce::Slider, kNumKnobs> sliders_;
    std::array<juce::Label, kNumKnobs> labels_;
    juce::Label message_;
};

// ---- provided: the layout, and the sliders wired to sliderMoved() ----------------------------------------------
inline SynthPanel::SynthPanel(dawlab::app::MainComponent& main) : main_(main) {
    for (int k = 0; k < kNumKnobs; ++k) {
        const KnobInfo& info = infoOf(static_cast<Knob>(k));
        juce::Slider& s = sliders_[static_cast<size_t>(k)];
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
        s.setRange(info.min, info.max);
        if (info.logarithmic)
            s.setSkewFactorFromMidPoint(std::sqrt(info.min * info.max));
        s.onValueChange = [this] { sliderMoved(); };
        s.setEnabled(false);
        addAndMakeVisible(s);
        labels_[static_cast<size_t>(k)].setText(info.label, juce::dontSendNotification);
        labels_[static_cast<size_t>(k)].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(labels_[static_cast<size_t>(k)]);
    }
    addAndMakeVisible(message_);
    setSize(7 * 90, 150);
}

inline void SynthPanel::resized() {
    auto area = getLocalBounds().reduced(4);
    message_.setBounds(area.removeFromBottom(20));
    const int w = area.getWidth() / kNumKnobs;
    for (int k = 0; k < kNumKnobs; ++k) {
        auto column = area.removeFromLeft(w);
        labels_[static_cast<size_t>(k)].setBounds(column.removeFromTop(18));
        sliders_[static_cast<size_t>(k)].setBounds(column);
    }
}

} // namespace ex8
