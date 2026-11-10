// Exercise 8 (starter): a synth settings panel.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex8_synth_panel.cpp. What the functions must do is in ../ex8_synth_panel.h.

#include "../ex8_synth_panel.h"

namespace ex8 {

juce::String toText(const VoiceSettings& settings) {
    // TODO: "key=value" for the seven knobs, joined by spaces, with enough digits that fromText() gets the same
    //       double back (try "%.15g", and 16 or 17 digits when 15 do not come back exactly).
    (void)settings;
    return {};
}

bool fromText(const juce::String& text, VoiceSettings& out) {
    // TODO: split at spaces; each part a known key, once, '=', a number inside the knob's range. All seven.
    (void)text;
    (void)out;
    return false;
}

void SynthPanel::showTrack(TrackId track) {
    // TODO: Engine::getTrackSynth() for an instrument track that is here; its settings into the sliders WITHOUT
    //       a notification (juce::dontSendNotification). Otherwise disable the sliders and say why.
    track_ = track;
}

void SynthPanel::sliderMoved() {
    // TODO: find the synth again by track_ (it may be gone); the seven slider values as VoiceSettings to it.
}

} // namespace ex8

#include "../ex8_check.h"
