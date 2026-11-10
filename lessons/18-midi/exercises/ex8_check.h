#pragma once
// Exercise 8 - the self-check (main). Included at the end of the starter and of the solution.

#include "app_rig.h"
#include "ex8_synth_panel.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace ex8::check {

using namespace ex18rig;

bool same(const VoiceSettings& a, const VoiceSettings& b) {
    for (int k = 0; k < kNumKnobs; ++k)
        if (valueOf(a, static_cast<Knob>(k)) != valueOf(b, static_cast<Knob>(k)))
            return false;
    return true;
}

// How bright a sound is: the energy of its differences against its own (0 for silence).
double brightness(const std::vector<float>& x, size_t from, size_t to) {
    double diff = 0.0, energy = 0.0;
    for (size_t i = from + 1; i < to; ++i) {
        diff += (x[i] - x[i - 1]) * static_cast<double>(x[i] - x[i - 1]);
        energy += x[i] * static_cast<double>(x[i]);
    }
    return energy > 0.0 ? diff / energy : 0.0;
}

// A clip of 8 bars with one long low note.
void longNote(Rig& rig, TrackId track) {
    MidiClipModel clip;
    clip.length = 8 * 3840;
    clip.played = {{0, 8 * 3840, 36, 110, 0}};
    rig.session.setMidiClips(track, {clip});
    rig.engine.applyPendingCommandsOffline();
}

void checkText() {
    const VoiceSettings defaults;
    VoiceSettings back;
    DAWLAB_CHECK(fromText(toText(defaults), back) && same(back, defaults));
    std::printf("  the default sound as text: %s\n", toText(defaults).toRawUTF8());
    std::mt19937 rng(8);
    int good = 0;
    for (int run = 0; run < 500; ++run) {
        VoiceSettings s;
        for (int k = 0; k < kNumKnobs; ++k) {
            const KnobInfo& info = infoOf(static_cast<Knob>(k));
            setValueOf(s, static_cast<Knob>(k), std::uniform_real_distribution<double>(info.min, info.max)(rng));
        }
        VoiceSettings r;
        good += fromText(toText(s), r) && same(r, s) ? 1 : 0;
    }
    DAWLAB_CHECK(good == 500);
    // Any order, more spaces: yes.
    VoiceSettings shuffled;
    DAWLAB_CHECK(fromText("level=0.5  sustain=1 release=2 decay=0.1 attack=0.01 resonance=2 cutoff=1000", shuffled) &&
                 shuffled.cutoffHz == 1000.0 && shuffled.level == 0.5f && shuffled.sustain == 1.0);
    const char* bad[] = {
        "",
        "cutoff=1000",                                                                                           // the rest is missing
        "cutoff=1000 resonance=2 attack=0.01 decay=0.1 sustain=1 release=2 level=0.5 cutoff=900",               // twice
        "cutoff=1000 resonance=2 attack=0.01 decay=0.1 sustain=1 release=2 volume=0.5",                         // unknown
        "cutoff=1000 resonance=2 attack=0.01 decay=0.1 sustain=1 release=2 level=1.5",                          // out of range
        "cutoff=20 resonance=2 attack=0.01 decay=0.1 sustain=1 release=2 level=0.5",                            // below
        "cutoff=1000Hz resonance=2 attack=0.01 decay=0.1 sustain=1 release=2 level=0.5",                        // not a number
        "cutoff= resonance=2 attack=0.01 decay=0.1 sustain=1 release=2 level=0.5",                              // empty
        "cutoff=nan resonance=2 attack=0.01 decay=0.1 sustain=1 release=2 level=0.5",                           // nan
        "cutoff 1000 resonance=2 attack=0.01 decay=0.1 sustain=1 release=2 level=0.5",                          // no '='
    };
    int refused = 0;
    for (const char* text : bad) {
        VoiceSettings untouched;
        untouched.cutoffHz = 123.0;
        const bool ok = !fromText(text, untouched) && untouched.cutoffHz == 123.0;
        if (!ok)
            std::printf("  NOT refused: \"%s\"\n", text);
        refused += ok ? 1 : 0;
    }
    std::printf("  500 random settings back exactly from their text; %d of %zu bad lines refused\n", refused, sizeof bad / sizeof bad[0]);
    DAWLAB_CHECK(refused == static_cast<int>(sizeof bad / sizeof bad[0]));
}

void checkPanel() {
    Rig rig;
    const TrackId a = rig.instrumentTrack(), b = rig.instrumentTrack();
    const TrackId audio = rig.main->addTrack(dawlab::engine::TrackFormat::Mono, false);
    rig.engine.applyPendingCommandsOffline();
    longNote(rig, a);
    SynthPanel panel(*rig.main);

    // Showing a track sends nothing; it shows what the synth has.
    VoiceSettings odd;
    odd.cutoffHz = 777.0;
    odd.release = 1.25;
    rig.engine.getTrackSynth(a)->setSettings(odd);
    panel.showTrack(a);
    DAWLAB_CHECK(panel.slider(Knob::Cutoff).isEnabled() && panel.slider(Knob::Cutoff).getValue() == 777.0 &&
                 panel.slider(Knob::Release).getValue() == 1.25);
    DAWLAB_CHECK(same(rig.engine.getTrackSynth(a)->getSettings(), odd));
    panel.showTrack(b);
    DAWLAB_CHECK(panel.slider(Knob::Cutoff).getValue() == VoiceSettings{}.cutoffHz && same(rig.engine.getTrackSynth(a)->getSettings(), odd));

    // A slider moves: the whole settings go to that track's synth, and only to it.
    panel.showTrack(a);
    rig.engine.getTransport().play();
    panel.slider(Knob::Cutoff).setValue(8000.0, juce::sendNotificationSync);
    const std::vector<float> bright = rig.play(48000);
    VoiceSettings expect = odd;
    expect.cutoffHz = 8000.0;
    DAWLAB_CHECK(same(rig.engine.getTrackSynth(a)->getSettings(), expect));
    DAWLAB_CHECK(same(rig.engine.getTrackSynth(b)->getSettings(), VoiceSettings{}));
    panel.slider(Knob::Cutoff).setValue(200.0, juce::sendNotificationSync);
    const std::vector<float> dark = rig.play(48000);
    const double hi = brightness(bright, 24000, 48000), lo = brightness(dark, 24000, 48000);
    std::printf("  one long note, cutoff 8000 Hz then 200 Hz from the panel: brightness %.5f, then %.5f\n", hi, lo);
    DAWLAB_CHECK(lo > 0.0 && hi > 10.0 * lo);
    // Level 0 while the note sounds: silence from the next block, and the note still there.
    panel.slider(Knob::Level).setValue(0.0, juce::sendNotificationSync);
    rig.play(512);
    const std::vector<float> silent = rig.play(4800);
    DAWLAB_CHECK(peakOf(silent) == 0.0f && rig.engineTrack(a)->getSynth()->activeVoices() == 1);
    panel.slider(Knob::Level).setValue(0.25, juce::sendNotificationSync);
    rig.play(512);
    DAWLAB_CHECK(peakOf(rig.play(4800)) > 0.01f);
    std::printf("  level 0 while the note sounds: silent, the note still held; back to 0.25: heard again\n");

    // Not an instrument; then the track is deleted under the panel.
    panel.showTrack(audio);
    DAWLAB_CHECK(!panel.slider(Knob::Cutoff).isEnabled() && panel.getMessage().contains("not an instrument"));
    panel.showTrack(b);
    rig.session.removeTrack(b);
    rig.engine.applyPendingCommandsOffline();
    panel.slider(Knob::Resonance).setValue(5.0, juce::sendNotificationSync); // must not reach the next track in b's slot
    const TrackId c = rig.instrumentTrack();
    DAWLAB_CHECK(!panel.slider(Knob::Resonance).isEnabled() && panel.getMessage().contains("No track"));
    DAWLAB_CHECK(same(rig.engine.getTrackSynth(c)->getSettings(), VoiceSettings{}));
    panel.showTrack(TrackId{});
    DAWLAB_CHECK(!panel.slider(Knob::Level).isEnabled());
    std::printf("  an audio track, a deleted track, no track: the sliders off, and why\n");
}

} // namespace ex8::check

int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    using namespace ex8::check;
    std::printf("Exercise 8 - a synth settings panel\n");
    checkText();
    checkPanel();
    return dawlab::test::summary("l18_ex8");
}
