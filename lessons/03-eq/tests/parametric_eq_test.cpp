// Regression test (self-test) for ParametricEq
//
// This is neither an example nor an exercise solution; it is the "contract test" for this chapter's DSP component dawlab::dsp::ParametricEq:
// it verifies, with numbers, every guarantee stated in the README and the authoring guide (theoretical curve = measurement, correct parameter-smoothing time,
// no clicks when changing type / toggling a band, new parameters still take effect when smoothing = 0, ...).
// Later chapters (the plugin in Chapter 13, the DAW in Chapter 14) use ParametricEq directly, so run this test first after changing the header.
//
// How to run: l03_test_parametric_eq (no arguments; ctest label: solution)
// Output: console text only; when everything passes, the last line is [PASS].

#include <dawlab/Common.h>
#include <dawlab/dsp/ParametricEq.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSr = 48000.0;

// ---- 1. Smoothing time: regardless of block size or how often the host resends parameters, the ramp length must be ≈ smoothingSeconds · sr ----
//
// How we measure: the same sequence of parameter calls goes into two identical EQs, one fed sin and the other cos (same frequency).
// For a (slowly varying) filter, sqrt(ys² + yc²) is "the gain at this instant", with no need to wait a full period.
// The gain of a Peak @ 1 kHz goes linearly (in the dB domain) from 0 dB to +12 dB, and the ramp is 20 ms = 960 samples,
// so the gain should cross +6 dB at around sample ~480 (plus up to 32 samples of coefficient-update delay and the filter's own delay).
enum class Resend { None, SameValues, OtherBandChanges };

int samplesToHalfGain(int blockSize, Resend resend) {
    ParametricEq eqS, eqC;
    for (ParametricEq* eq : {&eqS, &eqC}) {
        eq->prepare(kSr, 512, 1);
        eq->setBand(0, {EqBandType::Peak, 1000.0f, 1.0f, 0.0f, true});
        eq->setBand(1, {EqBandType::Peak, 5000.0f, 1.0f, 0.0f, true});
        eq->reset();
    }
    const int total = 4800;
    std::vector<float> s(static_cast<size_t>(total)), c(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i) {
        s[static_cast<size_t>(i)] = static_cast<float>(std::sin(kTwoPi * 1000.0 * i / kSr));
        c[static_cast<size_t>(i)] = static_cast<float>(std::cos(kTwoPi * 1000.0 * i / kSr));
    }
    // Run 100 samples first so the tick phase is not at 0 (simulating a parameter change in the middle of playback)
    const int startAt = 100;
    int crossed = -1;
    for (int start = 0, block = 0; start < total; start += blockSize, ++block) {
        const int len = std::min(blockSize, total - start);
        for (ParametricEq* eq : {&eqS, &eqC}) {
            if (start >= startAt && start - blockSize < startAt)
                eq->setBandGainDb(0, 12.0f); // change the target only once, at this moment
            else if (start > startAt && resend == Resend::SameValues)
                eq->setBand(0, eq->getBand(0)); // the host resends the same parameters at the start of every block
            else if (start > startAt && resend == Resend::OtherBandChanges)
                eq->setBandGainDb(1, (block % 2) ? 0.001f : 0.0f); // another band keeps changing -> a tick is forced on every block
        }
        float* ps = s.data() + start;
        float* pc = c.data() + start;
        eqS.process(AudioBlock(&ps, 1, len));
        eqC.process(AudioBlock(&pc, 1, len));
    }
    int changeAt = -1;
    for (int start = 0; start < total; start += blockSize)
        if (start >= startAt) {
            changeAt = start;
            break;
        }
    for (int i = changeAt; i < total; ++i) {
        const double amp = std::sqrt(double(s[size_t(i)]) * s[size_t(i)] + double(c[size_t(i)]) * c[size_t(i)]);
        if (gainToDb(static_cast<float>(amp)) >= 6.0f) {
            crossed = i - changeAt;
            break;
        }
    }
    return crossed;
}

void testRampLength() {
    std::printf("[1] ramp length (Peak 1 kHz 0 -> +12 dB, 20 ms = 960 samples; crossing +6 dB should happen at ~480 samples)\n");
    std::printf("    block   no resend   resend same values every block   other band changes every block\n");
    for (int block : {1, 8, 16, 48, 512}) {
        const int a = samplesToHalfGain(block, Resend::None);
        const int b = samplesToHalfGain(block, Resend::SameValues);
        const int c = samplesToHalfGain(block, Resend::OtherBandChanges);
        std::printf("    %5d   %9d   %29d   %30d\n", block, a, b, c);
        for (int v : {a, b, c}) {
            DAWLAB_CHECK(v >= 470);
            DAWLAB_CHECK(v <= 540);
        }
    }
}

// ---- 2. With smoothing = 0 (or shorter than one sample), parameter changes must still really be applied to the filter ----
void testZeroSmoothing() {
    std::printf("[2] smoothing 0: after changing parameters, the measured response must equal getMagnitudeDb\n");
    for (double seconds : {0.0, 0.000005}) {
        ParametricEq eq;
        eq.setSmoothingSeconds(seconds);
        eq.prepare(kSr, 512, 1);
        eq.setBand(0, {EqBandType::Peak, 1000.0f, 1.0f, 12.0f, true});
        AudioBuffer buf = makeSine(440.0, kSr, 0.1, 0.25f, 1);
        eq.process(buf.toBlock());
        eq.setBandFrequency(0, 4000.0f);
        eq.setBandGainDb(0, -12.0f);
        eq.process(buf.toBlock()); // Do not call reset(): reset would recompute the coefficients as a side effect and hide the bug
        for (double f : {1000.0, 4000.0}) {
            const double m = measureGainDb([&](float x) { return eq.processSample(0, x); }, f, kSr);
            std::printf("    smoothing %.6f s  %5.0f Hz: measured %+7.3f dB, getMagnitudeDb %+7.3f dB\n", seconds, f, m,
                        eq.getMagnitudeDb(f));
            DAWLAB_CHECK_NEAR(m, eq.getMagnitudeDb(f), 0.05);
        }
        // The same goes for changing the type (with smoothing 0 the mix also switches instantly)
        eq.setBandType(0, EqBandType::LowShelf);
        eq.process(buf.toBlock());
        const double m = measureGainDb([&](float x) { return eq.processSample(0, x); }, 200.0, kSr);
        DAWLAB_CHECK_NEAR(m, eq.getMagnitudeDb(200.0), 0.05);
        DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(200.0), -12.0, 0.2);
    }
}

// ---- 3. Theoretical curve = measurement (all six bands on, including 24 dB/oct) ----
void testCurveMatchesMeasurement() {
    std::printf("[3] All six bands on: getMagnitudeDb vs sine-wave measurement\n");
    ParametricEq eq;
    eq.prepare(kSr, 512, 1);
    eq.setBand(0, {EqBandType::LowCut, 60.0f, 0.707f, 0.0f, true});
    eq.setSlope(0, EqSlope::Db24);
    eq.setBand(1, {EqBandType::LowShelf, 120.0f, 0.707f, 3.0f, true});
    eq.setBand(2, {EqBandType::Peak, 300.0f, 1.5f, -4.0f, true});
    eq.setBand(3, {EqBandType::Notch, 3000.0f, 4.0f, 0.0f, true});
    eq.setBand(4, {EqBandType::HighShelf, 10000.0f, 0.707f, 2.0f, true});
    eq.setBand(5, {EqBandType::HighCut, 18000.0f, 0.707f, 0.0f, true});
    eq.reset();
    double maxDiff = 0.0;
    for (double f : {30.0, 60.0, 120.0, 300.0, 1000.0, 2500.0, 5000.0, 10000.0, 16000.0, 18000.0}) {
        eq.reset();
        const double m = measureGainDb([&](float x) { return eq.processSample(0, x); }, f, kSr);
        maxDiff = std::max(maxDiff, std::abs(m - eq.getMagnitudeDb(f)));
    }
    std::printf("    max difference %.4f dB\n", maxDiff);
    DAWLAB_CHECK(maxDiff < 0.05);

    // A 24 dB/oct cut is -3.01 dB at fc (two Butterworth sections multiplied), and so is a 12 dB/oct one
    ParametricEq cut;
    cut.prepare(kSr, 512, 1);
    for (EqSlope slope : {EqSlope::Db12, EqSlope::Db24}) {
        cut.setBand(0, {EqBandType::HighCut, 2000.0f, 0.707f, 0.0f, true});
        cut.setSlope(0, slope);
        cut.reset();
        const double m = measureGainDb([&](float x) { return cut.processSample(0, x); }, 2000.0, kSr);
        std::printf("    HighCut 2 kHz %s: measured at fc %+.3f dB\n", slope == EqSlope::Db24 ? "24 dB/oct" : "12 dB/oct", m);
        DAWLAB_CHECK_NEAR(m, -3.0103, 0.03);
        DAWLAB_CHECK_NEAR(cut.getMagnitudeDb(2000.0), -3.0103, 0.01);
    }
}

// ---- 4. After switching types back and forth, the steady-state response still equals getMagnitudeDb ----
void testTypeSwitching() {
    std::printf("[4] Switching type / slope / enable back and forth during playback; the final steady-state response = getMagnitudeDb\n");
    ParametricEq eq;
    eq.prepare(kSr, 64, 1);
    AudioBuffer noise = makeWhiteNoise(kSr, 0.5, 0.1f, 1);
    const EqBandType seq[] = {EqBandType::Peak, EqBandType::Notch, EqBandType::Peak, EqBandType::LowShelf,
                              EqBandType::HighCut, EqBandType::LowCut};
    eq.setBand(2, {EqBandType::Peak, 800.0f, 2.0f, 9.0f, true});
    int k = 0;
    AudioBlock whole = noise.toBlock();
    for (int start = 0; start + 64 <= noise.getNumSamples(); start += 64, ++k) {
        if (k % 7 == 0) // Switch every 7 blocks (about 9 ms, shorter than the 40 ms switch): a queued switch must also finish correctly
            eq.setBandType(2, seq[(k / 7) % 6]);
        if (k % 11 == 0)
            eq.setSlope(2, (k / 11) % 2 ? EqSlope::Db24 : EqSlope::Db12);
        if (k % 13 == 0)
            eq.setBandEnabled(2, (k / 13) % 2 == 0);
        eq.process(whole.getSubBlock(start, 64));
    }
    DAWLAB_CHECK(!hasNanOrInf(noise));
    eq.setBand(2, {EqBandType::Peak, 800.0f, 2.0f, 9.0f, true});
    eq.process(whole.getSubBlock(0, 4096)); // let the 40 ms switch run to completion
    for (double f : {200.0, 800.0, 3000.0}) {
        const double m = measureGainDb([&](float x) { return eq.processSample(0, x); }, f, kSr);
        std::printf("    %5.0f Hz: measured %+7.3f dB, getMagnitudeDb %+7.3f dB\n", f, m, eq.getMagnitudeDb(f));
        DAWLAB_CHECK_NEAR(m, eq.getMagnitudeDb(f), 0.05);
    }
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(800.0), 9.0, 0.01);
}

// ---- 5. A frequency set before prepare() is not permanently clamped by the 48 kHz limit ----
void testFrequencyBeforePrepare() {
    std::printf("[5] Set a 30 kHz HighCut before prepare, then prepare(96 kHz)\n");
    ParametricEq eq;
    eq.setBand(5, {EqBandType::HighCut, 30000.0f, 0.707f, 0.0f, true}); // e.g. a plugin restoring its state before prepareToPlay
    eq.prepare(96000.0, 512, 1);
    std::printf("    getBand(5).frequency = %.0f Hz\n", eq.getBand(5).frequency);
    DAWLAB_CHECK_NEAR(eq.getBand(5).frequency, 30000.0, 0.5);
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(30000.0), -3.0103, 0.01);
    const double m = measureGainDb([&](float x) { return eq.processSample(0, x); }, 30000.0, 96000.0);
    DAWLAB_CHECK_NEAR(m, -3.0103, 0.05);
    // Switch back to 48 kHz: the effective frequency is 0.49·fs, but the user's setting is preserved
    eq.prepare(48000.0, 512, 1);
    DAWLAB_CHECK_NEAR(eq.getBand(5).frequency, 30000.0, 0.5);
    const double m48 = measureGainDb([&](float x) { return eq.processSample(0, x); }, 10000.0, 48000.0);
    DAWLAB_CHECK_NEAR(m48, eq.getMagnitudeDb(10000.0), 0.05);
    // NaN fields are ignored
    eq.setBandFrequency(5, std::nanf(""));
    DAWLAB_CHECK_NEAR(eq.getBand(5).frequency, 30000.0, 0.5);
}

// ---- 6. No clicks when toggling a band or changing type: sine prediction residual ----
float maxSineResidual(const AudioBuffer& y, double freq) {
    const float* d = y.getReadPointer(0);
    const double c = 2.0 * std::cos(kTwoPi * freq / kSr);
    double m = 0.0;
    for (int i = 2; i < y.getNumSamples(); ++i)
        m = std::max(m, std::abs(d[i] - c * d[i - 1] + d[i - 2]));
    return static_cast<float>(m);
}

void testNoClicks() {
    std::printf("[6] Sine prediction residual when toggling a band / changing type (1 kHz, amplitude 0.25)\n");
    AudioBuffer y = makeSine(1000.0, kSr, 0.6, 0.25f, 1);
    const float dry = maxSineResidual(y, 1000.0);
    ParametricEq eq;
    eq.prepare(kSr, 512, 1);
    AudioBlock whole = y.toBlock();
    for (int start = 0, block = 0; start < y.getNumSamples(); start += 128, ++block) {
        if (block == 20)
            eq.setBand(0, {EqBandType::LowCut, 2000.0f, 0.707f, 0.0f, true});
        if (block == 80)
            eq.setBandType(0, EqBandType::Peak);
        if (block == 150)
            eq.setBandEnabled(0, false);
        eq.process(whole.getSubBlock(start, std::min(128, y.getNumSamples() - start)));
    }
    const float wet = maxSineResidual(y, 1000.0);
    std::printf("    original %.6f, ParametricEq %.6f\n", dry, wet);
    DAWLAB_CHECK(wet < 0.004f); // naive direct switching gives about 0.01-0.03 (Example 3)
}

} // namespace

int main() {
    std::printf("=== Chapter 03 ParametricEq regression test ===\n\n");
    testRampLength();
    testZeroSmoothing();
    testCurveMatchesMeasurement();
    testTypeSwitching();
    testFrequencyBeforePrepare();
    testNoClicks();
    return dawlab::test::summary("l03_test_parametric_eq");
}
