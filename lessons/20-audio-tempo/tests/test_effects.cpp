// test_effects: every InsertEffect adapter (Step 5).
//
// For each of the 19 effect types (+ the PureDelay test helper):
//   - the factory round-trips name <-> type <-> getName()
//   - the parameter table is sane: min < max, default inside, unique names, choices present
//   - setParameter clamps / rounds, normalized values round-trip
//   - it processes 2 s of drums + plucks while every parameter is randomised every block
//     ("fuzzing" the parameter API) without producing NaN / inf or exploding
//   - the reported latency is small and non-negative, and a pure delay of N reports N
// plus measured responses of the two adapters added last: Filter (chapter 02's state-variable
// filter, compared with chapter 02's RBJ biquads) and AmpChain (chapter 07).

#include "TestHelpers.h"

#include <cstring>
#include <iterator>
#include <random>
#include <set>
#include <string>

#include "TestTracks.h"

using namespace l15test;
using namespace dawlab::engine::test;

namespace {

void testTable(InsertEffect& fx) {
    std::set<std::string> names;
    for (int i = 0; i < fx.getNumParameters(); ++i) {
        const ParameterInfo info = fx.getParameterInfo(i);
        DAWLAB_CHECK(std::strlen(info.name) > 0);
        DAWLAB_CHECK(info.min < info.max);
        DAWLAB_CHECK(info.defaultValue >= info.min && info.defaultValue <= info.max);
        DAWLAB_CHECK(fx.getParameter(i) == info.defaultValue);
        names.insert(info.name);
        if (info.kind == ParameterKind::Choice || info.kind == ParameterKind::Bool) {
            DAWLAB_CHECK(info.choices != nullptr);
            DAWLAB_CHECK(info.numChoices == static_cast<int>(info.max - info.min) + 1);
        }
        if (info.logarithmic)
            DAWLAB_CHECK(info.min > 0.0f);
        // clamping
        fx.setParameter(i, info.max + 1000.0f);
        DAWLAB_CHECK(fx.getParameter(i) == info.max);
        fx.setParameter(i, info.min - 1000.0f);
        DAWLAB_CHECK(fx.getParameter(i) == info.min);
        // normalized round trip at the default
        const float n = info.toNormalized(info.defaultValue);
        DAWLAB_CHECK(n >= 0.0f && n <= 1.0f);
        DAWLAB_CHECK_NEAR(info.fromNormalized(n), info.defaultValue, 1e-3 * (info.max - info.min) + 1e-4);
        char text[64] = {};
        DAWLAB_CHECK(formatParameterValue(info, info.defaultValue, text, sizeof(text)) > 0);
        fx.setParameter(i, info.defaultValue);
    }
    DAWLAB_CHECK(static_cast<int>(names.size()) == fx.getNumParameters()); // unique names
    DAWLAB_CHECK(fx.getNumParameters() <= kMaxEffectParameters);
    // lookup by name
    if (fx.getNumParameters() > 0) {
        const ParameterInfo first = fx.getParameterInfo(0);
        DAWLAB_CHECK(fx.findParameter(first.name) == 0);
    }
    DAWLAB_CHECK(fx.findParameter("no such parameter") == -1);
    DAWLAB_CHECK(fx.findParameter("") == -1); // not the empty info of an index past the end
    DAWLAB_CHECK(!fx.setParameterByName("no such parameter", 1.0f));
    // an index outside the table has no info and no value, and setting it changes nothing
    for (const int outside : {-1, fx.getNumParameters()}) {
        fx.setParameter(outside, 0.7f);
        DAWLAB_CHECK(fx.getParameter(outside) == 0.0f);
        const ParameterInfo none = fx.getParameterInfo(outside);
        DAWLAB_CHECK(std::strlen(none.name) == 0 && none.min == 0.0f && none.max == 1.0f);
    }
    if (fx.getNumParameters() == 0)
        return;
    // by name, and back to the defaults
    const ParameterInfo first = fx.getParameterInfo(0);
    DAWLAB_CHECK(fx.setParameterByName(first.name, first.max));
    DAWLAB_CHECK(fx.getParameter(0) == first.max);
    for (int i = 0; i < fx.getNumParameters(); ++i)
        fx.setParameter(i, fx.getParameterInfo(i).min);
    fx.resetParametersToDefault();
    for (int i = 0; i < fx.getNumParameters(); ++i)
        DAWLAB_CHECK(fx.getParameter(i) == fx.getParameterInfo(i).defaultValue);
}

void testProcessing(InsertEffect& fx, std::mt19937& rng) {
    fx.prepare(kSampleRate, kBlockSize, 2);
    AudioBuffer drums = makeDrumLoop(kSampleRate, 1, 120.0);
    AudioBuffer plucks = makePluckSequence(kSampleRate, 2.0);
    const int n = std::min(drums.getNumSamples(), plucks.getNumSamples());
    AudioBuffer x(2, n);
    for (int ch = 0; ch < 2; ++ch) {
        x.addFrom(ch, 0, drums, ch, 0, n);
        x.addFrom(ch, 0, plucks, ch, 0, n);
    }
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (int pos = 0; pos < n; pos += kBlockSize) {
        const int len = std::min(kBlockSize, n - pos);
        if ((pos / kBlockSize) % 4 == 3) // every 4th block: randomise all parameters
            for (int i = 0; i < fx.getNumParameters(); ++i)
                fx.setParameterNormalized(i, u(rng));
        fx.process(x.toBlock().getSubBlock(pos, len));
    }
    DAWLAB_CHECK(!hasNanOrInf(x));
    DAWLAB_CHECK(peakAbs(x) < 1000.0f);
    DAWLAB_CHECK(fx.getLatencySamples() >= 0 && fx.getLatencySamples() < 16384);
    if (fx.hasMeter())
        DAWLAB_CHECK(std::isfinite(fx.getMeterDb()));

    // after reset + default parameters, silence in -> (almost) silence out
    fx.resetParametersToDefault();
    fx.reset();
    AudioBuffer silence(2, 8 * kBlockSize);
    for (int pos = 0; pos < silence.getNumSamples(); pos += kBlockSize) // never more than maxBlockSize
        fx.process(silence.toBlock().getSubBlock(pos, kBlockSize));
    DAWLAB_CHECK(peakAbs(silence) < 1e-3f);
}

void testDefaultsAreTransparentEnough() {
    // Effects whose default should leave a quiet sine (almost) untouched in level.
    for (const char* name : {"Gain", "ParametricEQ", "Delay"}) {
        auto fx = createEffect(name);
        DAWLAB_CHECK(fx != nullptr);
        if (!fx)
            continue;
        if (std::strcmp(name, "Delay") == 0)
            fx->setParameterByName("Mix", 0.0f);
        if (std::strcmp(name, "ParametricEQ") == 0)
            fx->setParameterByName("B3 Gain", 0.0f);
        fx->prepare(kSampleRate, kBlockSize, 2);
        AudioBuffer s = makeSine(1000.0, kSampleRate, 0.5, 0.25f);
        for (int pos = 0; pos < s.getNumSamples(); pos += kBlockSize)
            fx->process(s.toBlock().getSubBlock(pos, std::min(kBlockSize, s.getNumSamples() - pos)));
        const float level = rms(s.getReadPointer(0) + 12000, s.getNumSamples() - 12000);
        DAWLAB_CHECK_NEAR(gainToDb(level / (0.25f / std::sqrt(2.0f))), 0.0, 0.5);
    }
}

// Steady-state gain (dB) of `fx` for a sine of `hz` (0.5 s settle, then 0.5 s measured).
double measureGainDb(InsertEffect& fx, double hz) {
    fx.reset();
    AudioBuffer s = makeSine(hz, kSampleRate, 1.0, 0.25f, 2);
    for (int pos = 0; pos < s.getNumSamples(); pos += kBlockSize)
        fx.process(s.toBlock().getSubBlock(pos, std::min(kBlockSize, s.getNumSamples() - pos)));
    const int half = s.getNumSamples() / 2;
    const double amp = goertzelAmplitude(s.getReadPointer(0) + half, half, hz, kSampleRate);
    return 20.0 * std::log10(std::max(amp, 1e-12) / 0.25);
}

void testFilterEffect() {
    // The TPT state-variable filter and the RBJ cookbook biquad discretise the same analog
    // prototype with the same bilinear transform, so their magnitude responses must agree.
    FilterEffect f;
    f.prepare(kSampleRate, kBlockSize, 2);
    struct Case {
        int type;      // dsp::StateVariableFilter::Type: 0 LP, 1 HP, 2 BP, 3 Notch
        double cutoff, q, hz;
    };
    const Case cases[] = {
        {0, 1000.0, 0.707, 1000.0}, {0, 1000.0, 0.707, 100.0}, {0, 1000.0, 0.707, 8000.0}, {0, 1000.0, 4.0, 1000.0},
        {1, 1000.0, 0.707, 1000.0}, {1, 1000.0, 0.707, 125.0}, {2, 2000.0, 2.0, 2000.0},   {2, 2000.0, 2.0, 500.0},
        {3, 1000.0, 0.707, 4000.0},
    };
    double worst = 0.0;
    for (const Case& c : cases) {
        f.setParameter(FilterEffect::Type, static_cast<float>(c.type));
        f.setParameter(FilterEffect::Cutoff, static_cast<float>(c.cutoff));
        f.setParameter(FilterEffect::Resonance, static_cast<float>(c.q));
        const double measured = measureGainDb(f, c.hz);
        dsp::BiquadCoefficients ref;
        switch (c.type) {
        case 0: ref = dsp::BiquadCoefficients::makeLowpass(kSampleRate, c.cutoff, c.q); break;
        case 1: ref = dsp::BiquadCoefficients::makeHighpass(kSampleRate, c.cutoff, c.q); break;
        case 2: ref = dsp::BiquadCoefficients::makeBandpass(kSampleRate, c.cutoff, c.q); break;
        default: ref = dsp::BiquadCoefficients::makeNotch(kSampleRate, c.cutoff, c.q); break;
        }
        const double expected = ref.magnitudeDb(c.hz, kSampleRate);
        worst = std::max(worst, std::abs(measured - expected));
        DAWLAB_CHECK_NEAR(measured, expected, 0.05);
    }
    // At the cutoff a Q = 0.707 lowpass is at -3.01 dB, and the notch removes the tone.
    f.setParameter(FilterEffect::Type, 0.0f);
    f.setParameter(FilterEffect::Cutoff, 1000.0f);
    f.setParameter(FilterEffect::Resonance, 0.707f);
    const double lpAtCutoff = measureGainDb(f, 1000.0);
    f.setParameter(FilterEffect::Type, 3.0f);
    const double notchAtCutoff = measureGainDb(f, 1000.0);
    DAWLAB_CHECK_NEAR(lpAtCutoff, -3.01, 0.02);
    DAWLAB_CHECK(notchAtCutoff < -60.0);
    std::printf("  Filter vs RBJ biquad: worst difference %.4f dB over %zu cases; LP at fc %.2f dB, notch at fc %.1f dB\n",
                worst, sizeof(cases) / sizeof(cases[0]), lpAtCutoff, notchAtCutoff);
}

void testAmpChainEffect() {
    // Latency: both nonlinear stages oversample (prepare-time choice), the chain adds them up.
    AmpChainEffect off, os4;
    off.setParameter(AmpChainEffect::Oversampling, 0.0f); // 1x
    off.prepare(kSampleRate, kBlockSize, 2);
    os4.prepare(kSampleRate, kBlockSize, 2); // default 4x
    dsp::AmpChain ref;
    ref.setOversamplingFactorLog2(2);
    ref.prepare(kSampleRate, kBlockSize, 2);
    DAWLAB_CHECK(off.getLatencySamples() == 0);
    DAWLAB_CHECK(os4.getLatencySamples() > 0 && os4.getLatencySamples() == ref.getLatencySamples());

    // The default sound distorts: a clean 220 Hz sine at -20 dBFS comes out with a strong 3rd
    // harmonic, and the output stage after the nonlinear stages is a plain gain (-6 dB = -6 dB).
    const auto run = [](AmpChainEffect& fx, double& fundamentalDb, double& thirdDb, float& rmsDb) {
        fx.reset();
        AudioBuffer s = makeSine(220.0, kSampleRate, 1.0, 0.1f, 2);
        for (int pos = 0; pos < s.getNumSamples(); pos += kBlockSize)
            fx.process(s.toBlock().getSubBlock(pos, std::min(kBlockSize, s.getNumSamples() - pos)));
        const int half = s.getNumSamples() / 2;
        fundamentalDb = 20.0 * std::log10(goertzelAmplitude(s.getReadPointer(0) + half, half, 220.0, kSampleRate));
        thirdDb = 20.0 * std::log10(goertzelAmplitude(s.getReadPointer(0) + half, half, 660.0, kSampleRate));
        rmsDb = gainToDb(rms(s.getReadPointer(0) + half, half));
        DAWLAB_CHECK(!hasNanOrInf(s));
        DAWLAB_CHECK(peakAbs(s) < 1.0f);
    };
    double f1 = 0.0, h3 = 0.0, f1b = 0.0, h3b = 0.0;
    float rmsA = 0.0f, rmsB = 0.0f;
    run(os4, f1, h3, rmsA);
    os4.setParameter(AmpChainEffect::Output, -18.0f);
    run(os4, f1b, h3b, rmsB);
    std::printf("  AmpChain: latency %d samples at 4x (0 at 1x); 220 Hz at -20 dBFS -> fundamental %.1f dB, "
                "3rd harmonic %.1f dB (%.1f dB below), RMS %.2f dBFS; output -12 -> -18 dB: RMS %.2f dB\n",
                os4.getLatencySamples(), f1, h3, f1 - h3, static_cast<double>(rmsA), static_cast<double>(rmsB - rmsA));
    DAWLAB_CHECK(f1 - h3 < 20.0); // heavily distorted (a clean sine: no 3rd harmonic at all)
    DAWLAB_CHECK_NEAR(rmsB - rmsA, -6.0, 0.05);
}

void testParameterHandOver() {
    // setParameter() from "the UI" only reaches the DSP object at the next process():
    // a GainEffect at 0 dB, set to -6.02 dB, then processed -> x0.5 after the ramp.
    GainEffect g;
    g.prepare(kSampleRate, kBlockSize, 1);
    g.setParameter(GainEffect::Gain, -6.0206f);
    DAWLAB_CHECK_NEAR(g.getParameter(GainEffect::Gain), -6.0206, 1e-4);
    AudioBuffer ones(1, 48000);
    for (int i = 0; i < 48000; ++i)
        ones.setSample(0, i, 1.0f);
    for (int pos = 0; pos < 48000; pos += kBlockSize)
        g.process(ones.toBlock().getSubBlock(pos, std::min(kBlockSize, 48000 - pos)));
    DAWLAB_CHECK_NEAR(ones.getSample(0, 47999), 0.5, 1e-4);

    // Saturation's oversampling is prepare-time: the latency follows the choice at prepare().
    SaturationEffect s1, s2;
    s1.setParameter(SaturationEffect::Oversampling, 0.0f); // 1x
    s2.setParameter(SaturationEffect::Oversampling, 2.0f); // 4x
    s1.prepare(kSampleRate, kBlockSize, 2);
    s2.prepare(kSampleRate, kBlockSize, 2);
    DAWLAB_CHECK(s1.getLatencySamples() == 0);
    DAWLAB_CHECK(s2.getLatencySamples() > 0);

    // EQ bands map to the right parameter indices
    ParametricEqEffect eq;
    eq.setBand(2, true, dsp::EqBandType::Peak, 1000.0f, 1.0f, 6.0f);
    DAWLAB_CHECK(eq.getParameter(ParametricEqEffect::paramIndex(2, ParametricEqEffect::Gain)) == 6.0f);
    DAWLAB_CHECK(std::strcmp(eq.getParameterInfo(ParametricEqEffect::paramIndex(2, ParametricEqEffect::Gain)).name,
                             "B3 Gain") == 0);
}

// The names a project file stores, in the enum's order, and what a number that is no
// EffectType gives (a damaged file): "?" and no effect.
void testNamesAndUnknownTypes() {
    const char* const expected[] = {"Gain", "ParametricEQ", "Compressor", "Limiter", "Saturation",
                                    "Distortion", "Gate", "Expander", "DeEsser", "TransientShaper",
                                    "Delay", "Chorus", "Flanger", "Phaser", "Tremolo",
                                    "Reverb", "ConvolutionReverb", "Filter", "AmpChain", "PureDelay"};
    DAWLAB_CHECK(static_cast<int>(std::size(expected)) == kNumEffectTypes);
    for (int t = 0; t < kNumEffectTypes; ++t)
        DAWLAB_CHECK(std::strcmp(effectTypeName(static_cast<EffectType>(t)), expected[t]) == 0);
    for (const int t : {-1, kNumEffectTypes, 1000}) {
        DAWLAB_CHECK(std::strcmp(effectTypeName(static_cast<EffectType>(t)), "?") == 0);
        DAWLAB_CHECK(createEffect(static_cast<EffectType>(t)) == nullptr);
    }
    EffectType back = EffectType::Gain;
    DAWLAB_CHECK(!effectTypeFromName("?", back) && back == EffectType::Gain); // "?" names no type
}

// Edge cases of the knob mapping: an empty range, and a log taper that cannot start at 0.
void testNormalizedEdgeCases() {
    ParameterInfo fixed;
    fixed.min = fixed.max = 2.0f;
    DAWLAB_CHECK(fixed.toNormalized(2.0f) == 0.0f);
    ParameterInfo fromZero;
    fromZero.max = 10.0f;
    fromZero.logarithmic = true; // ln(v / 0) is no taper: the linear one is used
    DAWLAB_CHECK(fromZero.toNormalized(5.0f) == 0.5f);
    DAWLAB_CHECK(fromZero.fromNormalized(0.5f) == 5.0f);
}

std::string formatted(const ParameterInfo& info, float value) {
    char text[64] = {};
    formatParameterValue(info, value, text, sizeof(text));
    return text;
}

void testParameterText() {
    ParameterInfo f; // a wide float range
    f.min = -1000.0f;
    f.max = 1000.0f;
    f.unit = "dB";
    DAWLAB_CHECK(formatted(f, -18.0f) == "-18.0 dB");
    f.unit = ":1"; // a ratio has no space
    DAWLAB_CHECK(formatted(f, 4.0f) == "4.00:1");
    f.unit = "";
    DAWLAB_CHECK(formatted(f, 4.0f) == "4.00");
    DAWLAB_CHECK(formatted(f, 9.5f) == "9.50");
    DAWLAB_CHECK(formatted(f, 10.0f) == "10.0"); // fewer decimals from 10 and from 100 on
    DAWLAB_CHECK(formatted(f, 100.0f) == "100");
    DAWLAB_CHECK(formatted(f, -100.0f) == "-100");
    ParameterInfo i = f;
    i.kind = ParameterKind::Int;
    DAWLAB_CHECK(formatted(i, 4.0f) == "4");
    i.unit = "ms";
    DAWLAB_CHECK(formatted(i, 4.0f) == "4 ms");

    const ParameterInfo onOff = paramBool("Freeze", false);
    DAWLAB_CHECK(formatted(onOff, 1.0f) == "On" && formatted(onOff, 0.0f) == "Off");
    static constexpr const char* kLabels[] = {"one", "two", "three", "four"};
    ParameterInfo c;
    c.kind = ParameterKind::Choice;
    c.min = 1.0f; // the first label belongs to min
    c.max = 3.0f;
    c.choices = kLabels;
    c.numChoices = 3;
    DAWLAB_CHECK(formatted(c, 2.0f) == "two");
    c.numChoices = 2; // fewer labels than values: the last label is used
    DAWLAB_CHECK(formatted(c, 3.0f) == "two");
    c.choices = nullptr; // no labels at all: the number
    DAWLAB_CHECK(formatted(c, 2.0f) == "2.00");

    char untouched[4] = {'x', 'x', 'x', '\0'};
    DAWLAB_CHECK(formatParameterValue(f, 1.0f, untouched, 0) == 0 && untouched[0] == 'x');
}

// PureDelay: y[n] = x[n - D] on every channel, across blocks, and only inside the blocks it is given.
void testPureDelay() {
    constexpr int kDelay = 3, kLen = 32, kTotal = 2 * kLen;
    PureDelayEffect d(kDelay);
    d.prepare(kSampleRate, kBlockSize, 2);
    AudioBuffer x(2, kTotal + 1);
    for (int i = 0; i <= kTotal; ++i) {
        x.setSample(0, i, static_cast<float>(i + 1));
        x.setSample(1, i, -2.0f * static_cast<float>(i + 1));
    }
    d.process(x.toBlock().getSubBlock(0, kLen));
    d.process(x.toBlock().getSubBlock(kLen, kLen));
    for (int i = 0; i < kTotal; ++i) {
        const float expected = i < kDelay ? 0.0f : static_cast<float>(i + 1 - kDelay);
        DAWLAB_CHECK(x.getSample(0, i) == expected);
        DAWLAB_CHECK(x.getSample(1, i) == -2.0f * expected);
    }
    DAWLAB_CHECK(x.getSample(0, kTotal) == static_cast<float>(kTotal + 1)); // past the block: untouched
}

AudioBuffer processed(InsertEffect& fx, AudioBuffer x) {
    for (int pos = 0; pos < x.getNumSamples(); pos += kBlockSize)
        fx.process(x.toBlock().getSubBlock(pos, std::min(kBlockSize, x.getNumSamples() - pos)));
    return x;
}

// Level (dB re `amplitude`) of the tone `hz` in the second half of channel ch.
double toneDb(const AudioBuffer& y, int ch, double hz, double amplitude) {
    const int half = y.getNumSamples() / 2;
    const double amp = goertzelAmplitude(y.getReadPointer(ch) + half, half, hz, kSampleRate);
    return 20.0 * std::log10(std::max(amp, 1e-12) / amplitude);
}

// Each On/Off parameter reaches its DSP switch, the right way round.
void testSwitches() {
    { // EQ: a band only acts while it is enabled (+12 dB at 1 kHz)
        ParametricEqEffect eq;
        eq.prepare(kSampleRate, kBlockSize, 2);
        eq.setBand(2, true, dsp::EqBandType::Peak, 1000.0f, 1.0f, 12.0f);
        DAWLAB_CHECK_NEAR(toneDb(processed(eq, makeSine(1000.0, kSampleRate, 1.0, 0.1f)), 0, 1000.0, 0.1), 12.0, 0.5);
        eq.setParameter(ParametricEqEffect::paramIndex(2, ParametricEqEffect::Enabled), 0.0f);
        eq.reset();
        DAWLAB_CHECK_NEAR(toneDb(processed(eq, makeSine(1000.0, kSampleRate, 1.0, 0.1f)), 0, 1000.0, 0.1), 0.0, 0.5);
    }
    { // Compressor: a loud left side pulls a quiet right side down only while linked
        AudioBuffer x = makeSine(1000.0, kSampleRate, 1.0, 0.9f);
        for (int i = 0; i < x.getNumSamples(); ++i)
            x.setSample(1, i, x.getSample(1, i) * (0.02f / 0.9f));
        CompressorEffect c;
        c.setParameter(CompressorEffect::Threshold, -20.0f);
        c.setParameter(CompressorEffect::Ratio, 10.0f);
        c.prepare(kSampleRate, kBlockSize, 2);
        const double linked = toneDb(processed(c, x), 1, 1000.0, 0.02);
        c.setParameter(CompressorEffect::StereoLink, 0.0f);
        c.reset();
        const double alone = toneDb(processed(c, x), 1, 1000.0, 0.02);
        DAWLAB_CHECK_NEAR(alone, 0.0, 0.2);
        DAWLAB_CHECK(linked < -6.0);
    }
    { // AmpChain: the cabinet is a lowpass, an 8 kHz tone is much quieter through it
        AmpChainEffect a;
        a.prepare(kSampleRate, kBlockSize, 2);
        const double cabinet = toneDb(processed(a, makeSine(8000.0, kSampleRate, 1.0, 0.1f)), 0, 8000.0, 0.1);
        a.setParameter(AmpChainEffect::Cabinet, 0.0f);
        a.reset();
        const double open = toneDb(processed(a, makeSine(8000.0, kSampleRate, 1.0, 0.1f)), 0, 8000.0, 0.1);
        DAWLAB_CHECK(cabinet < open - 6.0);
    }
    { // DeEsser: Listen plays the detector's band (no 200 Hz); split band leaves the low band alone
        DeEsserEffect d;
        d.prepare(kSampleRate, kBlockSize, 2);
        AudioBuffer low = makeSine(200.0, kSampleRate, 1.0, 0.1f);
        DAWLAB_CHECK_NEAR(toneDb(processed(d, low), 0, 200.0, 0.1), 0.0, 0.5);
        d.setParameter(DeEsserEffect::Listen, 1.0f);
        d.reset();
        DAWLAB_CHECK(toneDb(processed(d, low), 0, 200.0, 0.1) < -20.0);
        d.setParameter(DeEsserEffect::Listen, 0.0f);
        AudioBuffer hiss = makeSine(6000.0, kSampleRate, 1.0, 0.5f); // far above the -20 dB threshold
        for (int ch = 0; ch < 2; ++ch)
            hiss.addFrom(ch, 0, low, ch, 0, low.getNumSamples());
        d.reset();
        const double split = toneDb(processed(d, hiss), 0, 200.0, 0.1);
        d.setParameter(DeEsserEffect::SplitBand, 0.0f);
        d.reset();
        const double wide = toneDb(processed(d, hiss), 0, 200.0, 0.1);
        DAWLAB_CHECK_NEAR(split, 0.0, 0.5);
        DAWLAB_CHECK(wide < -6.0);
    }
    { // Delay: an echo of the left side reaches the right side only in ping-pong
        DelayEffect d;
        d.setParameter(DelayEffect::Time, 10.0f);
        d.setParameter(DelayEffect::Feedback, 0.9f);
        d.setParameter(DelayEffect::Mix, 1.0f);
        d.prepare(kSampleRate, kBlockSize, 2);
        AudioBuffer x(2, kBlockSize * 4);
        x.setSample(0, 0, 1.0f);
        DAWLAB_CHECK(peakAbs(processed(d, x).getReadPointer(1), x.getNumSamples()) == 0.0f);
        d.setParameter(DelayEffect::PingPong, 1.0f);
        d.reset();
        DAWLAB_CHECK(peakAbs(processed(d, x).getReadPointer(1), x.getNumSamples()) > 0.1f);
    }
    { // Tremolo: the same gain on both sides, unless it auto-pans
        TremoloEffect t;
        t.setParameter(TremoloEffect::Depth, 1.0f);
        t.prepare(kSampleRate, kBlockSize, 2);
        AudioBuffer ones(2, static_cast<int>(kSampleRate) / 2);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < ones.getNumSamples(); ++i)
                ones.setSample(ch, i, 1.0f);
        AudioBuffer y = processed(t, ones);
        float sideDiff = 0.0f;
        for (int i = 0; i < y.getNumSamples(); ++i)
            sideDiff = std::max(sideDiff, std::abs(y.getSample(0, i) - y.getSample(1, i)));
        DAWLAB_CHECK(sideDiff == 0.0f);
        t.setParameter(TremoloEffect::AutoPan, 1.0f);
        t.reset();
        y = processed(t, ones);
        for (int i = 0; i < y.getNumSamples(); ++i)
            sideDiff = std::max(sideDiff, std::abs(y.getSample(0, i) - y.getSample(1, i)));
        DAWLAB_CHECK(sideDiff > 0.5f);
    }
    { // Reverb: a frozen tail stays, a 0.5 s decay is gone after 2 s
        const auto tailAfterTwoSeconds = [](bool freeze) {
            ReverbEffect r;
            r.setParameter(ReverbEffect::Decay, 0.5f);
            r.setParameter(ReverbEffect::Mix, 1.0f);
            r.prepare(kSampleRate, kBlockSize, 2);
            AudioBuffer x(2, static_cast<int>(kSampleRate) * 2);
            for (int ch = 0; ch < 2; ++ch)
                x.setSample(ch, 0, 1.0f);
            const int start = static_cast<int>(kSampleRate / 4); // the tail is in by now
            for (int pos = 0; pos < x.getNumSamples(); pos += kBlockSize) {
                if (pos >= start)
                    r.setParameter(ReverbEffect::Freeze, freeze ? 1.0f : 0.0f);
                r.process(x.toBlock().getSubBlock(pos, std::min(kBlockSize, x.getNumSamples() - pos)));
            }
            return rms(x.getReadPointer(0) + x.getNumSamples() - kBlockSize, kBlockSize);
        };
        const float frozen = tailAfterTwoSeconds(true), decayed = tailAfterTwoSeconds(false);
        DAWLAB_CHECK(frozen > 1e-3f && frozen > 100.0f * decayed);
    }
}

// ConvolutionReverb at Mix 0: the dry signal, delayed by the FFT partition (the latency).
void testConvolutionReverbDry() {
    ConvolutionReverbEffect c;
    c.setParameter(ConvolutionReverbEffect::Mix, 0.0f);
    c.prepare(kSampleRate, kBlockSize, 2);
    c.reset(); // no sound left over: the mix starts at its value instead of gliding there
    const int latency = c.getLatencySamples();
    AudioBuffer x(2, kBlockSize * 8);
    x.setSample(0, 100, 1.0f);
    const AudioBuffer y = processed(c, x);
    DAWLAB_CHECK_NEAR(y.getSample(0, 100 + latency), 1.0, 1e-5);
    DAWLAB_CHECK_NEAR(peakAbs(y.getReadPointer(0), 100 + latency), 0.0, 1e-5);
    DAWLAB_CHECK_NEAR(peakAbs(y.getReadPointer(0) + 101 + latency, y.getNumSamples() - 101 - latency), 0.0, 1e-5);
}

// Phaser: an odd number of stages is the even number below it; 2, 4 and 12 all sound different.
void testPhaserStages() {
    const auto run = [](float stages) {
        PhaserEffect p;
        p.setParameter(PhaserEffect::Stages, stages);
        p.prepare(kSampleRate, kBlockSize, 2);
        return processed(p, makeWhiteNoise(kSampleRate, 0.25));
    };
    const AudioBuffer two = run(2.0f), four = run(4.0f), five = run(5.0f), twelve = run(12.0f);
    DAWLAB_CHECK(maxAbsDiff(four, five) == 0.0f);
    DAWLAB_CHECK(maxAbsDiff(two, four) > 1e-2f);
    DAWLAB_CHECK(maxAbsDiff(four, twelve) > 1e-2f);
}

} // namespace

int main() {
    std::mt19937 rng(1234);
    int created = 0;
    for (int t = 0; t < kNumEffectTypes; ++t) {
        const auto type = static_cast<EffectType>(t);
        auto fx = createEffect(type);
        DAWLAB_CHECK(fx != nullptr);
        if (!fx)
            continue;
        ++created;
        DAWLAB_CHECK(std::strcmp(fx->getName(), effectTypeName(type)) == 0);
        EffectType back{};
        DAWLAB_CHECK(effectTypeFromName(fx->getName(), back) && back == type);
        testTable(*fx);
        testProcessing(*fx, rng);
        std::printf("  %-18s %2d params  latency %4d samples  %s\n", fx->getName(), fx->getNumParameters(),
                    fx->getLatencySamples(), fx->hasMeter() ? "meter" : "");
    }
    DAWLAB_CHECK(created == 20);
    DAWLAB_CHECK(kNumUserEffectTypes == 19);
    DAWLAB_CHECK(createEffect("NoSuchEffect") == nullptr);

    PureDelayEffect d(123);
    d.prepare(kSampleRate, kBlockSize, 2);
    DAWLAB_CHECK(d.getLatencySamples() == 123);

    testDefaultsAreTransparentEnough();
    testParameterHandOver();
    testFilterEffect();
    testAmpChainEffect();
    testNamesAndUnknownTypes();
    testNormalizedEdgeCases();
    testParameterText();
    testPureDelay();
    testSwitches();
    testConvolutionReverbDry();
    testPhaserStages();
    return dawlab::test::summary("l15_test_effects");
}
