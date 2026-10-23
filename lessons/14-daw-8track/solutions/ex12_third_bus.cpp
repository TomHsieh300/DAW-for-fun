// l14_ex12_solution -- Exercise 12: a third FX bus (solution).
//
// What it shows
//   - The engine is "static": the number of FX buses is the compile-time constant
//     engine::kNumBuses (Constants.h). Every array that depends on it (sends per strip, bus
//     buffers, strips in the mixer, ChannelRef::bus indices, the GUI's send knobs and bus strips)
//     is sized from that one constant. This program is linked against l14_daw_app_3bus, a copy of
//     the app library compiled with -DDAWLAB_ENGINE_NUM_BUSES=3 (app/CMakeLists.txt) - nothing
//     else had to change.
//   - configureThirdBus(): turns bus C into a "Doubler" bus (Chorus at 100 % wet + an EQ that
//     cuts the lows) and feeds it from the Pad and the Lead with PRE-fader sends.
//   - The GUI follows: 12 strips (8 tracks + 3 buses + master), 3 send knobs per track strip, and
//     the knob for C really drives ChannelStrip::setSendLevelDb(2, ...).
//
// Run
//   build/bin/Debug/l14_ex12_solution.exe
//
// Output
//   dawlab_output/l14_ex12_third_bus.png   the mixer with three bus strips
//   dawlab_output/l14_ex12_bus_c_only.wav  bars 1-4: only what comes back from bus C
//   Self-check: strip and knob counts, the knob -> engine link, signal on bus C even with the
//   source faders at -inf (pre-fader), and silence on bus C when its sends are off.

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include "MainComponent.h"
#include "MixerView.h"
#include "Session.h"

#include <cstdio>

using namespace dawlab;
using namespace dawlab::engine;
using namespace dawlab::app;

static_assert(kNumBuses == 3, "link this program against l14_daw_app_3bus (DAWLAB_ENGINE_NUM_BUSES=3)");

namespace ex12 {

constexpr int kDoublerBus = 2; // bus C
constexpr int kPad = 3, kLead = 5;

// Bus C = "Doubler": Chorus (100 % wet, deep) followed by an EQ low cut at 200 Hz; the Pad and the
// Lead are sent to it pre-fader at -6 dB, so the doubled sound stays when their faders go down.
void configureThirdBus(Session& session) {
    const ChannelRef bus = ChannelRef::bus(kDoublerBus);
    session.setStripName(bus, "Doubler");
    if (InsertEffect* chorus = session.setBuiltinInsert(bus, 0, EffectType::Chorus)) {
        chorus->setParameterByName("Mix", 1.0f);
        chorus->setParameterByName("Depth", 6.0f);
        chorus->setParameterByName("Voices", 3.0f);
    }
    if (InsertEffect* eq = session.setBuiltinInsert(bus, 1, EffectType::ParametricEq)) {
        eq->setParameterByName("B1 Enabled", 1.0f); // band 1 is the low cut
        eq->setParameterByName("B1 Frequency", 200.0f);
    }
    for (int t : {kPad, kLead}) {
        ChannelStrip& s = session.getEngine().getMixer().getTrack(t);
        s.setSendLevelDb(kDoublerBus, -6.0f);
        s.setSendPreFader(kDoublerBus, true);
    }
    session.getEngine().getMixer().getBus(kDoublerBus).setGainDb(-3.0f);
}

} // namespace ex12

namespace {
AudioBuffer renderBars(Engine& engine, int bars) {
    engine.applyPendingCommandsOffline();
    RenderOptions ro;
    ro.numSamples = static_cast<int64_t>(bars * engine.getTransport().getSamplesPerBar());
    return renderOffline(engine, ro);
}
} // namespace

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    using namespace ex12;

    MainComponent main(MainComponent::Mode::Offline);
    main.setSize(1500, 900);
    DAWLAB_CHECK(main.loadDemoSongNow() == 0);
    Session& session = main.getSession();
    Engine& engine = main.getEngine();
    configureThirdBus(session);
    engine.applyPendingCommandsOffline();

    // the GUI follows kNumBuses
    MixerView& mixer = main.getMixer();
    std::printf("mixer strips: %d (8 tracks + %d buses + master), send knobs per track: %d\n", mixer.getNumStrips(),
                kNumBuses, mixer.getStrip(0).getNumSendKnobs());
    DAWLAB_CHECK(mixer.getNumStrips() == 12);
    DAWLAB_CHECK(mixer.getStrip(0).getNumSendKnobs() == 3);
    DAWLAB_CHECK(mixer.getStrip(kNumTracks + kDoublerBus).getChannel() == ChannelRef::bus(kDoublerBus));
    if (juce::Slider* knob = mixer.getStrip(0).getSendKnob(kDoublerBus)) {
        knob->setValue(-20.0, juce::sendNotificationSync);
        DAWLAB_CHECK_NEAR(engine.getMixer().getTrack(0).getSendLevelDb(kDoublerBus), -20.0, 0.05);
        knob->setValue(FaderLaw::kBottomDb, juce::sendNotificationSync); // back to off
        DAWLAB_CHECK(engine.getMixer().getTrack(0).getSendLevelDb(kDoublerBus) <= kMinusInfinityDb);
    }
    main.tick();
    DAWLAB_CHECK(session.getStripName(ChannelRef::bus(kDoublerBus)) == "Doubler");
    DAWLAB_CHECK(session.getNumUsedSlots(ChannelRef::bus(kDoublerBus)) == 2);

    // Listen to bus C alone: mute buses A and B, pull the Pad and Lead faders to -inf. Their
    // pre-fader sends still feed bus C; nothing else reaches the master except the dry tracks,
    // which we mute as well (bus C is solo-safe like A and B, but here mute is simpler).
    Mixer& mix = engine.getMixer();
    for (int t = 0; t < kNumTracks; ++t)
        if (t != kPad && t != kLead)
            mix.getTrack(t).setMute(true);
    mix.getBus(0).setMute(true);
    mix.getBus(1).setMute(true);
    mix.getTrack(kPad).setGainDb(kMinusInfinityDb);
    mix.getTrack(kLead).setGainDb(kMinusInfinityDb);
    const AudioBuffer busOnly = renderBars(engine, 8);
    const StripStats busStats = mix.getBus(kDoublerBus).getStats();
    const StripStats padStats = mix.getTrack(kPad).getStats();
    std::printf("bus C only: master RMS %.1f dBFS, bus C RMS %.1f dBFS, Pad strip (fader -inf) peak %.1f dBFS\n",
                static_cast<double>(gainToDb(rms(busOnly, 0))), static_cast<double>(busStats.rmsDb),
                static_cast<double>(padStats.peakDb));
    DAWLAB_CHECK(busStats.rmsDb > -45.0f);
    DAWLAB_CHECK(gainToDb(rms(busOnly, 0)) > -50.0f);
    DAWLAB_CHECK(padStats.peakDb <= -99.0f);
    writeWav(outputPath("l14_ex12_bus_c_only.wav"), busOnly, engine.getSampleRate());

    // sends off -> bus C silent
    mix.getTrack(kPad).setSendLevelDb(kDoublerBus, kMinusInfinityDb);
    mix.getTrack(kLead).setSendLevelDb(kDoublerBus, kMinusInfinityDb);
    const AudioBuffer silent = renderBars(engine, 2);
    std::printf("sends off: master peak %.1f dBFS\n", static_cast<double>(gainToDb(peakAbs(silent))));
    DAWLAB_CHECK(peakAbs(silent) == 0.0f);

    // a picture of the mixer with three bus strips
    main.tick();
    const juce::Image image = mixer.createComponentSnapshot(mixer.getLocalBounds());
    const juce::File png = juce::File::getCurrentWorkingDirectory().getChildFile(outputPath("l14_ex12_third_bus.png"));
    png.deleteFile();
    {
        juce::FileOutputStream out(png);
        juce::PNGImageFormat format;
        DAWLAB_CHECK(out.openedOk() && format.writeImageToStream(image, out));
    }
    return test::summary("l14_ex12");
}
