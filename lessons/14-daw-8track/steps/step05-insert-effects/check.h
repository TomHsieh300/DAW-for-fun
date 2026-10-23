#pragma once
// check.h - the self-check of Step 5 (insert effects). Defines main(); included at the END of
// starter/step05.cpp and solution/step05.cpp.
//
// Tests only what Step 5 adds: parameter clamping and tapers, the dirty-mask hand-over, the
// chain order and bypass, and inserts on a track and on the master.

#include <dawlab/Common.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>

namespace step05_check {

using namespace dawlab;
using namespace minidaw;

constexpr double kSampleRate = 48000.0;
constexpr float kCentre = 0.70710678f;

// Test instrument: counts how often applyParameter() reaches the "DSP", adds an offset.
class ProbeEffect final : public EffectBase {
public:
    ProbeEffect() : EffectBase(kInfos, 2) {}
    const char* getName() const noexcept override { return "Probe"; }
    void prepare(double, int, int) override { finishPrepare(); }
    void reset() noexcept override {}
    int applied = 0;
    float offset = 0.0f;

protected:
    void applyParameter(int index, float v) noexcept override {
        ++applied;
        if (index == 0)
            offset = v;
    }
    void processDsp(AudioBlock block) noexcept override {
        for (int ch = 0; ch < block.getNumChannels(); ++ch)
            for (int i = 0; i < block.getNumSamples(); ++i)
                block.getChannelPointer(ch)[i] += offset;
    }

private:
    static constexpr ParameterInfo kInfos[2] = {{"Offset", "", -1.0f, 1.0f, 0.0f, ParameterKind::Float, false},
                                                {"Steps", "", 0.0f, 8.0f, 4.0f, ParameterKind::Int, false}};
};

inline AudioBuffer dcBlock(float v, int n = 64) {
    AudioBuffer b(kNumChannels, n);
    for (int ch = 0; ch < kNumChannels; ++ch)
        for (int i = 0; i < n; ++i)
            b.setSample(ch, i, v);
    return b;
}

inline int run() {
    std::printf("Step 5: insert effects (build-along)\n");

    // ---- 1) ParameterInfo -----------------------------------------------------------------------
    const ParameterInfo thr{"Threshold", "dB", -60.0f, 0.0f, -18.0f, ParameterKind::Float, false};
    const ParameterInfo freq{"Cutoff", "Hz", 20.0f, 20000.0f, 1000.0f, ParameterKind::Float, true};
    const ParameterInfo steps{"Steps", "", 0.0f, 8.0f, 4.0f, ParameterKind::Int, false};
    DAWLAB_CHECK(thr.clamp(-100.0f) == -60.0f && thr.clamp(5.0f) == 0.0f && thr.clamp(-12.5f) == -12.5f);
    DAWLAB_CHECK(thr.clamp(std::numeric_limits<float>::quiet_NaN()) == -18.0f); // NaN -> default
    DAWLAB_CHECK(steps.clamp(2.6f) == 3.0f && steps.clamp(99.0f) == 8.0f);
    std::printf("  knob centre: Threshold %.1f dB (linear), Cutoff %.1f Hz (log, expected 632.5)\n",
                thr.fromNormalized(0.5f), freq.fromNormalized(0.5f));
    DAWLAB_CHECK_NEAR(thr.fromNormalized(0.5f), -30.0, 1e-4);
    DAWLAB_CHECK_NEAR(thr.toNormalized(-15.0f), 0.75, 1e-6);
    DAWLAB_CHECK_NEAR(freq.fromNormalized(0.5f), 632.456, 0.01);
    DAWLAB_CHECK_NEAR(freq.toNormalized(632.456f), 0.5, 1e-5);
    DAWLAB_CHECK_NEAR(freq.fromNormalized(2.0f), 20000.0, 0.01); // n is clamped to 0..1
    double worstRoundTrip = 0.0;
    for (int k = 0; k <= 10; ++k) {
        const float n = static_cast<float>(k) / 10.0f;
        worstRoundTrip = std::max(worstRoundTrip, static_cast<double>(std::abs(freq.toNormalized(freq.fromNormalized(n)) - n)));
    }
    DAWLAB_CHECK(worstRoundTrip < 1e-5);

    // ---- 2) EffectBase: clamped storage, the dirty mask, values set before prepare() ---------------
    ProbeEffect probe;
    probe.setParameter(0, 0.25f); // before prepare: remembered, applied by finishPrepare()
    probe.prepare(kSampleRate, 512, kNumChannels);
    const int afterPrepare = probe.applied;
    DAWLAB_CHECK(probe.offset == 0.25f);
    probe.setParameter(0, 0.5f);
    probe.setParameter(0, 5.0f); // newest value wins, clamped to 1
    probe.setParameter(1, 2.4f);
    probe.setParameter(7, 1.0f); // invalid index: ignored
    DAWLAB_CHECK(probe.getParameter(0) == 1.0f && probe.getParameter(1) == 2.0f);
    AudioBuffer x = dcBlock(0.0f);
    probe.process(x.toBlock());
    std::printf("  three setParameter() calls on two parameters -> %d applyParameter() calls (expected 2)\n",
                probe.applied - afterPrepare);
    DAWLAB_CHECK(probe.applied - afterPrepare == 2); // several changes of one parameter collapse into one
    DAWLAB_CHECK(x.getSample(0, 0) == 1.0f);
    probe.process(x.toBlock());
    DAWLAB_CHECK(probe.applied - afterPrepare == 2); // nothing changed: nothing applied

    // ---- 3) InsertChain: order and bypass -----------------------------------------------------------
    {
        InsertChain chain;
        auto* offset = new ProbeEffect();
        offset->prepare(kSampleRate, 512, kNumChannels);
        offset->setParameter(0, 0.1f);
        auto* gain = new GainEffect();
        gain->prepare(kSampleRate, 512, kNumChannels);
        gain->setParameter(0, 6.0206f); // x2
        DAWLAB_CHECK(chain.insertAt(-1, offset) == nullptr && chain.insertAt(-1, gain) == nullptr);
        AudioBuffer b = dcBlock(0.2f);
        chain.process(b.toBlock());
        std::printf("  chain [offset +0.1, gain x2] on 0.2: %.4f (expected (0.2 + 0.1) x 2 = 0.6000)\n", b.getSample(0, 5));
        DAWLAB_CHECK_NEAR(b.getSample(0, 5), 0.6, 1e-5);
        offset->setBypassed(true);
        AudioBuffer c = dcBlock(0.2f);
        chain.process(c.toBlock());
        DAWLAB_CHECK_NEAR(c.getSample(1, 5), 0.4, 1e-5);
    }

    // ---- 4) Through the engine: a Gain insert on a track, a Compressor on the master ---------------
    {
        Engine engine;
        engine.prepare(kSampleRate, 512);
        auto dc = std::make_shared<AudioBuffer>(1, 48000);
        for (int i = 0; i < dc->getNumSamples(); ++i)
            dc->setSample(0, i, 0.1f);
        engine.setTrackClips(0, {Clip::fromBuffer(dc, 0)});
        InsertEffect* g = engine.addInsertNow(engine.getMixer().getTrack(0), createEffect("Gain"));
        DAWLAB_CHECK(g != nullptr && engine.getMixer().getTrack(0).getInserts().getNumInserts() == 1);
        if (g)
            g->setParameter(0, 6.0206f);
        engine.getTransport().play();
        AudioBuffer out(kNumChannels, 512);
        engine.processBlock(out.toBlock());
        std::printf("  DC 0.1 with a +6.02 dB Gain insert on track 1: %.4f (expected 0.2 x 0.7071 = 0.1414)\n",
                    out.getSample(0, 100));
        DAWLAB_CHECK_NEAR(out.getSample(0, 100), 0.2 * kCentre, 1e-5);

        InsertEffect* comp = engine.addInsertNow(engine.getMixer().getMaster(), createEffect("Compressor"));
        DAWLAB_CHECK(comp != nullptr && std::string(comp ? comp->getName() : "") == "Compressor");
        if (comp) {
            comp->setParameter(CompressorEffect::Threshold, -30.0f);
            comp->setParameter(CompressorEffect::Ratio, 10.0f);
            comp->setParameter(CompressorEffect::Attack, 0.1f);
        }
        for (int k = 0; k < 40; ++k)
            engine.processBlock(out.toBlock());
        const float squashed = out.getSample(0, 511);
        std::printf("  + Compressor on the master (-30 dB, 10:1): %.4f, gain reduction %.1f dB\n", squashed,
                    comp ? comp->getMeterDb() : 0.0f);
        DAWLAB_CHECK(squashed < 0.2f * kCentre * 0.5f);
        DAWLAB_CHECK(comp != nullptr && comp->hasMeter() && comp->getMeterDb() > 6.0f);
        if (comp)
            comp->setBypassed(true);
        engine.processBlock(out.toBlock());
        DAWLAB_CHECK_NEAR(out.getSample(0, 100), 0.2 * kCentre, 1e-5);
    }
    DAWLAB_CHECK(createEffect("NoSuchEffect") == nullptr);

    return dawlab::test::summary("l14_step05");
}

} // namespace step05_check

int main() { return step05_check::run(); }
