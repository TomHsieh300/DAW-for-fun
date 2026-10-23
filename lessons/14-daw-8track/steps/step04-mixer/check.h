#pragma once
// check.h - the self-check of Step 4 (mixer). Defines main(); included at the END of
// starter/step04.cpp and solution/step04.cpp.
//
// Tests only what Step 4 adds. Every track plays a constant ("DC") clip, so each gain shows up
// directly as an output sample value: fader, pan laws, balance on the master, mute, solo and
// the fader ramp.

#include <dawlab/Common.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace step04_check {

using namespace dawlab;
using namespace minidaw;

constexpr double kSampleRate = 48000.0;
constexpr float kCentre = 0.70710678f; // cos(pi/4)

inline Clip dcClip(float value) {
    auto b = std::make_shared<AudioBuffer>(1, 96000); // 2 s
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
    // Renders `samples` (several blocks, so every 10 ms / 50 ms ramp has settled) and returns
    // the last output sample of channel ch.
    float settle(int ch, int samples = 4096) {
        for (int done = 0; done < samples; done += 512)
            engine.processBlock(out.toBlock());
        return out.getSample(ch, 511);
    }
};

inline int run() {
    std::printf("Step 4: the mixer (build-along)\n");

    // ---- computeStripGains() on its own ---------------------------------------------------------
    float l = 0.0f, r = 0.0f;
    ChannelStrip::computeStripGains(0.0f, 0.0f, dsp::PanLaw::ConstantPower, PanMode::PanLaw, l, r);
    DAWLAB_CHECK_NEAR(l, kCentre, 1e-5);
    DAWLAB_CHECK_NEAR(r, kCentre, 1e-5);
    ChannelStrip::computeStripGains(0.0f, 0.0f, dsp::PanLaw::ConstantPower, PanMode::Balance, l, r);
    DAWLAB_CHECK(l == 1.0f && r == 1.0f); // balance: centre = unity
    ChannelStrip::computeStripGains(-6.0f, 0.5f, dsp::PanLaw::ConstantPower, PanMode::PanLaw, l, r);
    std::printf("  fader -6 dB, pan +0.5         : L %.4f (%.2f dB), R %.4f (%.2f dB)   (expected 0.1918, 0.4630)\n", l,
                gainToDb(l), r, gainToDb(r));
    DAWLAB_CHECK_NEAR(l, 0.1918, 1e-4);
    DAWLAB_CHECK_NEAR(r, 0.4630, 1e-4);
    ChannelStrip::computeStripGains(kMinusInfinityDb, 0.0f, dsp::PanLaw::ConstantPower, PanMode::PanLaw, l, r);
    DAWLAB_CHECK(l == 0.0f && r == 0.0f); // fader at the bottom = silence, not 10^(-100/20)

    // ---- one track, DC 0.5 ----------------------------------------------------------------------
    {
        Rig rig;
        rig.engine.setTrackClips(0, {dcClip(0.5f)});
        const float centre = rig.settle(0);
        std::printf("  DC 0.5, fader 0 dB, centre     : %.4f (expected 0.3536)\n", centre);
        DAWLAB_CHECK_NEAR(centre, 0.5 * kCentre, 1e-5);
        DAWLAB_CHECK_NEAR(rig.mixer().getMaster().getMaxPeakLinear(), 0.5 * kCentre, 1e-5);
        rig.mixer().getTrack(0).setGainDb(-6.0206f);
        DAWLAB_CHECK_NEAR(rig.settle(0), 0.25 * kCentre, 1e-5);
        rig.mixer().getTrack(0).setGainDb(0.0f);
        rig.mixer().getTrack(0).setPan(-1.0f);
        const float hardL = rig.settle(0), hardR = rig.settle(1);
        std::printf("  pan hard left                  : L %.4f, R %.4f (expected 0.5, 0)\n", hardL, hardR);
        DAWLAB_CHECK_NEAR(hardL, 0.5, 1e-5);
        DAWLAB_CHECK_NEAR(hardR, 0.0, 1e-6);
        rig.mixer().getTrack(0).setPan(0.0f);
        rig.mixer().getTrack(0).setPanLaw(dsp::PanLaw::Linear);
        DAWLAB_CHECK_NEAR(rig.settle(0), 0.25, 1e-5);
        rig.mixer().getTrack(0).setPanLaw(dsp::PanLaw::Minus4_5dB);
        const float m45 = rig.settle(0);
        std::printf("  Minus4_5dB law, centre         : %.2f dB relative to the clip (expected -4.52)\n",
                    gainToDb(m45 / 0.5f));
        DAWLAB_CHECK_NEAR(gainToDb(m45 / 0.5f), -4.515, 0.01);
        rig.mixer().getTrack(0).setPanLaw(dsp::PanLaw::ConstantPower);
        rig.mixer().getMaster().setGainDb(-6.0206f);
        DAWLAB_CHECK_NEAR(rig.settle(0), 0.5 * kCentre * 0.5, 1e-5);
        rig.mixer().getMaster().setGainDb(0.0f);
        rig.mixer().getMaster().setPan(1.0f); // master balance hard right: L off, R unchanged
        DAWLAB_CHECK_NEAR(rig.settle(0), 0.0, 1e-6);
        DAWLAB_CHECK_NEAR(rig.settle(1), 0.5 * kCentre, 1e-5);
    }

    // ---- two tracks: mute and solo ----------------------------------------------------------------
    {
        Rig rig;
        rig.engine.setTrackClips(0, {dcClip(0.5f)});
        rig.engine.setTrackClips(1, {dcClip(0.25f)});
        DAWLAB_CHECK_NEAR(rig.settle(0), 0.75 * kCentre, 1e-5);
        rig.mixer().getTrack(0).setMute(true);
        const float muted = rig.settle(0);
        rig.mixer().getTrack(0).setMute(false);
        rig.mixer().getTrack(1).setSolo(true);
        const float solo2 = rig.settle(0);
        rig.mixer().getTrack(0).setSolo(true);
        const float soloBoth = rig.settle(0);
        rig.mixer().getTrack(1).setMute(true); // mute wins over solo; track 2 still "soloed"
        const float soloBothMute2 = rig.settle(0);
        std::printf("  mute 1 / solo 2 / solo both / + mute 2: %.4f %.4f %.4f %.4f (expected 0.1768 0.1768 0.5303 0.3536)\n",
                    muted, solo2, soloBoth, soloBothMute2);
        DAWLAB_CHECK_NEAR(muted, 0.25 * kCentre, 1e-5);
        DAWLAB_CHECK_NEAR(solo2, 0.25 * kCentre, 1e-5);
        DAWLAB_CHECK_NEAR(soloBoth, 0.75 * kCentre, 1e-5);
        DAWLAB_CHECK_NEAR(soloBothMute2, 0.5 * kCentre, 1e-5);
        DAWLAB_CHECK(rig.mixer().isTrackAudible(0));
        DAWLAB_CHECK(!rig.mixer().isTrackAudible(1));
        DAWLAB_CHECK(!rig.mixer().isTrackAudible(2)); // empty, not soloed while others are
    }

    // ---- the fader ramp: 0 dB -> -20 dB while playing -----------------------------------------------
    {
        Engine engine;
        engine.prepare(kSampleRate, 512);
        engine.setTrackClips(0, {dcClip(0.5f)});
        engine.getTransport().play();
        AudioBuffer out(kNumChannels, 9600);
        engine.processBlock(out.toBlock().getSubBlock(0, 2400));
        engine.getMixer().getTrack(0).setGainDb(-20.0f);
        engine.processBlock(out.toBlock().getSubBlock(2400, 7200));
        float maxStep = 0.0f;
        for (int i = 1; i < out.getNumSamples(); ++i)
            maxStep = std::max(maxStep, std::abs(out.getSample(0, i) - out.getSample(0, i - 1)));
        std::printf("  fader 0 -> -20 dB while playing: max step %.6f (expected < 0.001), settles at %.5f (expected 0.03536)\n",
                    maxStep, out.getSample(0, 9599));
        DAWLAB_CHECK(maxStep < 0.001f);
        DAWLAB_CHECK_NEAR(out.getSample(0, 9599), 0.5 * kCentre * 0.1, 1e-5);
        DAWLAB_CHECK(out.getSample(0, 2400 + 1200) > out.getSample(0, 9599)); // still ramping half-way
    }

    return dawlab::test::summary("l14_step04");
}

} // namespace step04_check

int main() { return step04_check::run(); }
