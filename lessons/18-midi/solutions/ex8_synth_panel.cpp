// Exercise 8 (solution): a synth settings panel.
//
// Build and run:  l18_ex8_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex8_synth_panel.cpp -> l18_ex8_starter

#include "../exercises/ex8_synth_panel.h"

#include <cstdio>
#include <cstdlib>
#include <set>

namespace ex8 {

juce::String toText(const VoiceSettings& settings) {
    juce::StringArray parts;
    for (int k = 0; k < kNumKnobs; ++k) {
        // The fewest digits that come back as the same double: 0.9, not 0.90000000000000002.
        const double v = valueOf(settings, static_cast<Knob>(k));
        char number[40];
        for (int digits = 15; digits <= 17; ++digits) {
            std::snprintf(number, sizeof number, "%.*g", digits, v);
            if (std::strtod(number, nullptr) == v)
                break;
        }
        parts.add(juce::String(infoOf(static_cast<Knob>(k)).key) + "=" + juce::String(number));
    }
    return parts.joinIntoString(" ");
}

bool fromText(const juce::String& text, VoiceSettings& out) {
    VoiceSettings s;
    std::set<int> seen;
    juce::StringArray parts;
    parts.addTokens(text, " ", "");
    parts.removeEmptyStrings();
    for (const juce::String& part : parts) {
        const int eq = part.indexOfChar('=');
        if (eq <= 0)
            return false;
        const juce::String key = part.substring(0, eq), value = part.substring(eq + 1);
        int knob = -1;
        for (int k = 0; k < kNumKnobs && knob < 0; ++k)
            if (key == infoOf(static_cast<Knob>(k)).key)
                knob = k;
        if (knob < 0 || !seen.insert(knob).second)
            return false;
        // A number, all of it: "4000" yes, "4000Hz", "" and "nan" no.
        const juce::String trimmed = value.trim();
        if (trimmed.isEmpty() || !trimmed.containsOnly("0123456789.-+eE"))
            return false;
        const double v = std::strtod(trimmed.toRawUTF8(), nullptr); // correctly rounded, unlike a quick parser
        const KnobInfo& info = infoOf(static_cast<Knob>(knob));
        if (!(v >= info.min && v <= info.max))
            return false;
        setValueOf(s, static_cast<Knob>(knob), v);
    }
    if (static_cast<int>(seen.size()) != kNumKnobs)
        return false;
    out = s;
    return true;
}

namespace {

// The track's synth, if it is still here and an instrument; otherwise nullptr and why.
dawlab::engine::SynthInstrument* synthOf(dawlab::app::MainComponent& main, TrackId track, juce::String& why) {
    if (!track.isValid() || !main.getSession().hasTrack(track)) {
        why = "No track: choose an instrument track";
        return nullptr;
    }
    if (!main.getSession().isInstrument(track)) {
        why = main.getSession().getStripName(main.getSession().trackRef(track)) + " is not an instrument track";
        return nullptr;
    }
    why = {};
    return main.getEngine().getTrackSynth(track);
}

} // namespace

void SynthPanel::showTrack(TrackId track) {
    track_ = track;
    juce::String why;
    dawlab::engine::SynthInstrument* synth = synthOf(main_, track, why);
    for (int k = 0; k < kNumKnobs; ++k) {
        juce::Slider& s = sliders_[static_cast<size_t>(k)];
        s.setEnabled(synth != nullptr);
        if (synth != nullptr)
            s.setValue(valueOf(synth->getSettings(), static_cast<Knob>(k)), juce::dontSendNotification); // showing is not changing
    }
    message_.setText(synth != nullptr ? "Synth of " + main_.getSession().getStripName(main_.getSession().trackRef(track)) : why,
                     juce::dontSendNotification);
}

void SynthPanel::sliderMoved() {
    juce::String why;
    dawlab::engine::SynthInstrument* synth = synthOf(main_, track_, why);
    if (synth == nullptr) {
        for (juce::Slider& s : sliders_)
            s.setEnabled(false);
        message_.setText(why, juce::dontSendNotification);
        return;
    }
    VoiceSettings settings;
    for (int k = 0; k < kNumKnobs; ++k)
        setValueOf(settings, static_cast<Knob>(k), sliders_[static_cast<size_t>(k)].getValue());
    synth->setSettings(settings);
}

} // namespace ex8

#include "../exercises/ex8_check.h"
