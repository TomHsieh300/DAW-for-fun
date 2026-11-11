#include "SynthPanel.h"

#include "DawLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace dawlab::app {

const SynthPanel::KnobInfo& SynthPanel::infoOf(Knob knob) noexcept {
    // The ranges the project file accepts are wider (ProjectFormat.cpp); these are what a hand needs.
    static const std::array<KnobInfo, kNumKnobs> table{{
        {"Cutoff (Hz)", 50.0, 16000.0, true},
        {"Resonance", 0.5, 10.0, false},
        {"Attack (s)", 0.001, 2.0, true},
        {"Decay (s)", 0.001, 5.0, true},
        {"Sustain", 0.0, 1.0, false},
        {"Release (s)", 0.001, 5.0, true},
        {"Level", 0.0, 1.0, false},
    }};
    return table[static_cast<size_t>(knob)];
}

double SynthPanel::valueOf(const engine::VoiceSettings& s, Knob knob) noexcept {
    switch (knob) {
    case Cutoff: return s.cutoffHz;
    case Resonance: return s.resonance;
    case Attack: return s.attack;
    case Decay: return s.decay;
    case Sustain: return s.sustain;
    case Release: return s.release;
    case Level: return s.level;
    case kNumKnobs: break;
    }
    return 0.0;
}

void SynthPanel::setValueOf(engine::VoiceSettings& s, Knob knob, double v) noexcept {
    switch (knob) {
    case Cutoff: s.cutoffHz = v; break;
    case Resonance: s.resonance = v; break;
    case Attack: s.attack = v; break;
    case Decay: s.decay = v; break;
    case Sustain: s.sustain = v; break;
    case Release: s.release = v; break;
    case Level: s.level = static_cast<float>(v); break;
    case kNumKnobs: break;
    }
}

SynthPanel::SynthPanel(Session& session) : session_(session) {
    for (int k = 0; k < kNumKnobs; ++k) {
        const Knob knob = static_cast<Knob>(k);
        const KnobInfo& info = infoOf(knob);
        juce::Slider& s = sliders_[static_cast<size_t>(k)];
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
        s.setRange(info.min, info.max);
        if (info.logarithmic)
            s.setSkewFactorFromMidPoint(std::sqrt(info.min * info.max));
        s.onDragStart = [this] { beginGesture(); };
        s.onDragEnd = [this] { endGesture(); };
        s.onValueChange = [this, knob] { setKnob(knob, sliders_[static_cast<size_t>(knob)].getValue()); };
        addAndMakeVisible(s);
        labels_[static_cast<size_t>(k)].setText(info.label, juce::dontSendNotification);
        labels_[static_cast<size_t>(k)].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(labels_[static_cast<size_t>(k)]);
    }
    addAndMakeVisible(message_);
    session_.addChangeListener(this);
    setSize(7 * 92, 156);
    refresh();
}

SynthPanel::~SynthPanel() { session_.removeChangeListener(this); }

void SynthPanel::showTrack(engine::TrackId track) {
    track_ = track;
    refresh();
}

void SynthPanel::refresh() {
    juce::String text;
    enabled_ = false;
    if (!track_.isValid() || !session_.hasTrack(track_))
        text = "No track: select an instrument track";
    else if (!session_.isInstrument(track_))
        text = session_.getStripName(session_.trackRef(track_)) + " is not an instrument track";
    else if (session_.getPluginInstrument(track_) != nullptr)
        text = session_.getStripName(session_.trackRef(track_)) + " plays a plugin; these are the built-in synth's settings";
    else {
        enabled_ = true;
        text = "Synth of " + session_.getStripName(session_.trackRef(track_));
    }
    const engine::VoiceSettings sound = session_.getInstrumentSound(track_).synth;
    for (int k = 0; k < kNumKnobs; ++k) {
        juce::Slider& s = sliders_[static_cast<size_t>(k)];
        s.setEnabled(enabled_);
        if (session_.hasTrack(track_)) // showing is not changing: no notification
            s.setValue(valueOf(sound, static_cast<Knob>(k)), juce::dontSendNotification);
    }
    message_.setText(text, juce::dontSendNotification);
}

void SynthPanel::beginGesture() {
    session_.getHistory().endMerge();
    static std::uint32_t nextKey = 0x53590000u; // "SY": every drag its own undo step
    gestureKey_ = ++nextKey;
}

void SynthPanel::setKnob(Knob knob, double value) {
    if (!session_.hasTrack(track_) || !session_.isInstrument(track_) || session_.getPluginInstrument(track_) != nullptr) {
        refresh(); // the track changed under the panel: show it, send nothing
        return;
    }
    engine::VoiceSettings sound = session_.getInstrumentSound(track_).synth;
    const KnobInfo& info = infoOf(knob);
    const double clamped = std::clamp(value, info.min, info.max);
    if (valueOf(sound, knob) == (knob == Level ? static_cast<double>(static_cast<float>(clamped)) : clamped))
        return;
    setValueOf(sound, knob, clamped);
    session_.setSynthSettings(track_, sound, gestureKey_); // key 0 (no gesture open): a step of its own
}

void SynthPanel::endGesture() {
    gestureKey_ = 0;
    session_.getHistory().endMerge();
}

void SynthPanel::resized() {
    auto area = getLocalBounds().reduced(4);
    message_.setBounds(area.removeFromBottom(20));
    const int w = area.getWidth() / kNumKnobs;
    for (int k = 0; k < kNumKnobs; ++k) {
        auto column = area.removeFromLeft(w);
        labels_[static_cast<size_t>(k)].setBounds(column.removeFromTop(18));
        sliders_[static_cast<size_t>(k)].setBounds(column);
    }
}

SynthPanelWindow::SynthPanelWindow(Session& session, juce::LookAndFeel* lookAndFeel)
    : juce::DocumentWindow("Synth", DawColours::panel(), juce::DocumentWindow::closeButton) {
    setLookAndFeel(lookAndFeel);
    setUsingNativeTitleBar(true);
    auto panel = std::make_unique<SynthPanel>(session);
    panel_ = panel.get();
    setContentOwned(panel.release(), true);
    setResizable(false, false);
}

SynthPanelWindow::~SynthPanelWindow() { setLookAndFeel(nullptr); }

} // namespace dawlab::app
