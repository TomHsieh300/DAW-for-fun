#pragma once
// check.h - the self-check of Step 6 (sends and buses). Defines main(); included at the END of
// starter/step06.cpp and solution/step06.cpp.
//
// Tests only what Step 6 adds: post- and pre-fader sends, bus returns, mute before the send
// taps, solo-safe buses and the send ramp. DC clips again, so every gain is a sample value.
// g = 0.7071 is a track's centre pan; buses and master are at unity (balance mode).

#include <dawlab/Common.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace step06_check {

using namespace dawlab;
using namespace minidaw;

constexpr double kSampleRate = 48000.0;
constexpr float g = 0.70710678f;

inline Clip dcClip(float value) {
    auto b = std::make_shared<AudioBuffer>(1, 96000);
    for (int i = 0; i < b->getNumSamples(); ++i)
        b->setSample(0, i, value);
    return Clip::fromBuffer(b, 0);
}

struct Rig {
    Engine engine;
    AudioBuffer out{kNumChannels, 512};
    Rig() {
        engine.prepare(kSampleRate, 512);
        engine.getTransport().play();
    }
    Mixer& mixer() { return engine.getMixer(); }
    float settle(int samples = 4096) {
        for (int done = 0; done < samples; done += 512)
            engine.processBlock(out.toBlock());
        return out.getSample(0, 511);
    }
};

inline int run() {
    std::printf("Step 6: sends and buses (build-along)\n");

    // ---- the worked example of docs/step-06-sends-buses.md -------------------------------------------
    {
        Rig rig;
        rig.engine.setTrackClips(0, {dcClip(0.5f)});
        rig.engine.setTrackClips(1, {dcClip(0.25f)});
        Track& t1 = rig.mixer().getTrack(0);
        Track& t2 = rig.mixer().getTrack(1);
        t1.setSendLevelDb(0, -6.0206f); // post-fader (default) to bus A
        t2.setGainDb(kMinusInfinityDb); // fader down...
        t2.setSendPreFader(0, true);    // ...but a pre-fader send at 0 dB
        t2.setSendLevelDb(0, 0.0f);
        const float out = rig.settle();
        const float busA = rig.mixer().getBus(0).getMaxPeakLinear();
        std::printf("  direct 0.5 g + bus A (0.5 g x 0.5 post + 0.25 pre) : out %.4f (expected 0.7803), bus A %.4f (expected 0.4268)\n",
                    out, busA);
        DAWLAB_CHECK_NEAR(out, 0.5 * g + 0.5 * g * 0.5 + 0.25, 1e-4);
        DAWLAB_CHECK_NEAR(busA, 0.5 * g * 0.5 + 0.25, 1e-4);
        rig.mixer().getBus(0).setMute(true);
        const float busMuted = rig.settle();
        rig.mixer().getBus(0).setMute(false);
        t2.setMute(true); // a muted track sends nothing, also not pre-fader
        const float t2Muted = rig.settle();
        std::printf("  bus A muted: %.4f (expected 0.3536); track 2 muted: %.4f (expected 0.5303)\n", busMuted, t2Muted);
        DAWLAB_CHECK_NEAR(busMuted, 0.5 * g, 1e-4);
        DAWLAB_CHECK_NEAR(t2Muted, 0.5 * g + 0.5 * g * 0.5, 1e-4);
        rig.mixer().getBus(0).setGainDb(-6.0206f); // the bus fader scales only the return
        DAWLAB_CHECK_NEAR(rig.settle(), 0.5 * g + 0.5 * g * 0.5 * 0.5, 1e-4);
    }

    // ---- one track feeding both buses ---------------------------------------------------------------
    {
        Rig rig;
        rig.engine.setTrackClips(0, {dcClip(0.5f)});
        rig.mixer().getTrack(0).setSendLevelDb(0, 0.0f);
        rig.mixer().getTrack(0).setSendLevelDb(1, -6.0206f);
        const float both = rig.settle();
        std::printf("  one track, send A 0 dB + send B -6 dB: %.4f (expected 0.3536 x 2.5 = 0.8839)\n", both);
        DAWLAB_CHECK_NEAR(both, 0.5 * g * 2.5, 1e-4);
        // post-fader: pulling the track fader down pulls its sends down too
        rig.mixer().getTrack(0).setGainDb(-6.0206f);
        DAWLAB_CHECK_NEAR(rig.settle(), 0.5 * g * 2.5 * 0.5, 1e-4);
    }

    // ---- solo keeps the soloed track's reverb: buses are solo-safe ---------------------------------
    {
        Rig rig;
        rig.engine.setTrackClips(0, {dcClip(0.5f)});
        rig.engine.setTrackClips(1, {dcClip(0.25f)});
        rig.mixer().getTrack(0).setSendLevelDb(0, 0.0f);
        rig.mixer().getTrack(1).setSendLevelDb(0, 0.0f);
        rig.mixer().getTrack(0).setSolo(true);
        const float solo = rig.settle();
        std::printf("  solo track 1, both tracks send to bus A: %.4f (expected 0.5 g + 0.5 g = 0.7071)\n", solo);
        DAWLAB_CHECK_NEAR(solo, 0.5 * g * 2.0, 1e-4);
    }

    // ---- the send level is ramped -------------------------------------------------------------------
    {
        Engine engine;
        engine.prepare(kSampleRate, 512);
        engine.setTrackClips(0, {dcClip(0.5f)});
        engine.getTransport().play();
        AudioBuffer out(kNumChannels, 7200);
        engine.processBlock(out.toBlock().getSubBlock(0, 1200));
        engine.getMixer().getTrack(0).setSendLevelDb(0, 0.0f); // off -> 0 dB while playing
        engine.processBlock(out.toBlock().getSubBlock(1200, 6000));
        float maxStep = 0.0f;
        for (int i = 1; i < out.getNumSamples(); ++i)
            maxStep = std::max(maxStep, std::abs(out.getSample(0, i) - out.getSample(0, i - 1)));
        std::printf("  send switched on while playing: max step %.6f (expected < 0.001), ends at %.4f (expected 0.7071)\n",
                    maxStep, out.getSample(0, 7199));
        DAWLAB_CHECK(maxStep < 0.001f);
        DAWLAB_CHECK_NEAR(out.getSample(0, 7199), 0.5 * g * 2.0, 1e-4);
    }

    return dawlab::test::summary("l14_step06");
}

} // namespace step06_check

int main() { return step06_check::run(); }
