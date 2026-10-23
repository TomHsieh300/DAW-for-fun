// Example 3: EQ parameter automation (sweeping a peak's frequency) -- naive vs smoothed, quantifying the clicks
//
// What it demonstrates:
//   - A +12 dB, Q = 6 peak whose center frequency sweeps logarithmically from 150 Hz to 6 kHz over 4 seconds (the classic wah sound).
//   - The host sends a new frequency only every 100 ms (like turning a MIDI controller knob, or very coarse automation).
//   - Five approaches:
//       reference  : recompute the coefficients at every sample along the ideal sweep curve (most expensive, smoothest; used as the gold standard).
//       naive      : swap the Biquad coefficients as soon as a parameter arrives, with no smoothing in between -> stair-stepped parameters = zipper noise.
//       smooth/1   : let ParametricEq smooth it (100 ms linear ramp), recomputing the coefficients every 1 sample.
//       smooth/32  : same, but recompute the coefficients every 32 samples (ParametricEq's default).
//       smooth/512 : same, but recompute only every 512 samples (= one block).
//   - Quantifying the click: the analysis signal is a 1 kHz sine (amplitude 0.25). As the peak sweeps past 1 kHz, the output's
//     "loudness envelope" should rise and fall smoothly; we cut the output into 1 ms frames and compute each frame's RMS (in dB),
//     and the dB difference between adjacent frames is the "loudness jump per millisecond". A smooth sweep changes by only a few
//     tenths of a dB per millisecond; a jump of several dB at once is an audible click.
//   - The listening signal is a 110 Hz band-limited sawtooth (30 harmonics), which is more instrument-like.
//   - Part 3: changes that cannot be interpolated, such as "switching a band on/off" and "changing the band type". The naive approach (switching the Biquad directly)
//     vs ParametricEq (mix fade-out -> switch -> fade-in). The measure is the "sine prediction residual":
//     a pure sine satisfies y[n] = 2cos(ω)·y[n-1] - y[n-2], so r[n] = y[n] - 2cos(ω)·y[n-1] + y[n-2]
//     is 0 for a steady sine; when the amplitude or phase changes "slowly" r stays small, and at an instantaneous jump (click) r suddenly becomes large.
// How to run: l03_eq_automation (no arguments)
// Output (build directory/dawlab_output/):
//   l03_sweep_sine_naive.wav / l03_sweep_sine_smooth.wav (the 1 kHz sine used for analysis)
//   l03_sweep_saw_reference.wav / l03_sweep_saw_naive.wav / l03_sweep_saw_smooth.wav (sawtooth wah)
//   l03_switch_naive.wav / l03_switch_smooth.wav (part 3: band on/off and type change)
// What you should hear: the naive version has a regular "tick-tick-tick" (once every 100 ms, i.e. a 10 Hz
//   flutter) while the peak sweeps near the signal frequency; the smooth and reference versions are a continuous, smooth wah. The difference is clearest with the sine version.
//   At the end DAWLAB_CHECK self-checks (the smoothed versions have no jump > 1 dB/ms, and the residual for on/off / type change is at least 5x smaller than naive),
//   printing [PASS] if all hold; otherwise it returns non-zero and ctest reports a failure.

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/ParametricEq.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kSeconds = 4.0;
constexpr double kFreqStart = 150.0, kFreqEnd = 6000.0;
constexpr float kPeakGainDb = 12.0f, kPeakQ = 6.0f;
constexpr double kAutomationIntervalSec = 0.1; // the host sends parameters every 100 ms
constexpr double kToneHz = 1000.0;             // sine wave used for analysis
constexpr double kFrameMs = 1.0;               // frame length for the envelope analysis
constexpr float kJumpThresholdDb = 1.0f;       // a jump of more than 1 dB within 1 ms counts as one "click"

// Band-limited sawtooth: x = (2/π) Σ (-1)^(k+1) sin(2π k f t) / k, k = 1..harmonics
AudioBuffer makeSaw(double f0, int harmonics, double seconds, float amplitude) {
    AudioBuffer b(1, secondsToSamples(seconds, kSr));
    for (int i = 0; i < b.getNumSamples(); ++i) {
        double v = 0.0;
        for (int k = 1; k <= harmonics; ++k)
            v += ((k % 2) ? 1.0 : -1.0) * std::sin(kTwoPi * k * f0 * i / kSr) / k;
        b.setSample(0, i, static_cast<float>(amplitude * v * 2.0 / kPi));
    }
    return b;
}

// Sweep curve: logarithmic interpolation (the same number of octaves per second)
double sweepFrequency(double t) {
    return kFreqStart * std::pow(kFreqEnd / kFreqStart, std::clamp(t / kSeconds, 0.0, 1.0));
}

// reference: recompute the coefficients at every sample (ideal sweep)
AudioBuffer runReference(const AudioBuffer& input) {
    AudioBuffer out = input;
    Biquad bq;
    bq.prepare(kSr, kBlockSize, 1);
    float* d = out.getWritePointer(0);
    for (int i = 0; i < out.getNumSamples(); ++i) {
        bq.setCoefficients(BiquadCoefficients::makePeak(kSr, sweepFrequency(i / kSr), kPeakQ, kPeakGainDb));
        d[i] = bq.processSample(0, d[i]);
    }
    return out;
}

// naive: swap the coefficients directly every 100 ms, with no smoothing in between
AudioBuffer runNaive(const AudioBuffer& input) {
    AudioBuffer out = input;
    const int n = out.getNumSamples();
    const int automationInterval = secondsToSamples(kAutomationIntervalSec, kSr);
    Biquad bq;
    bq.prepare(kSr, kBlockSize, 1);
    float* d = out.getWritePointer(0);
    for (int start = 0; start < n; start += kBlockSize) {
        const int len = std::min(kBlockSize, n - start);
        for (int idx = start; idx < start + len; ++idx) {
            if (idx % automationInterval == 0)
                bq.setCoefficients(BiquadCoefficients::makePeak(kSr, sweepFrequency(idx / kSr), kPeakQ, kPeakGainDb));
            d[idx] = bq.processSample(0, d[idx]);
        }
    }
    return out;
}

// smooth: simulate the host sending a new frequency to ParametricEq between blocks every 100 ms, then processing the whole block
AudioBuffer runSmoothed(const AudioBuffer& input, int coefficientUpdateInterval) {
    AudioBuffer out = input;
    const int n = out.getNumSamples();
    const int automationInterval = secondsToSamples(kAutomationIntervalSec, kSr);
    ParametricEq eq;
    eq.setSmoothingSeconds(kAutomationIntervalSec); // the ramp is as long as the automation interval, so it just meets the next update
    eq.setCoefficientUpdateInterval(coefficientUpdateInterval);
    eq.prepare(kSr, kBlockSize, 1);
    eq.setBand(0, {EqBandType::Peak, static_cast<float>(kFreqStart), kPeakQ, kPeakGainDb, true});
    eq.reset();
    AudioBlock whole(out);
    for (int start = 0; start < n; start += kBlockSize) {
        const int len = std::min(kBlockSize, n - start);
        for (int idx = start; idx < start + len; ++idx)
            if (idx % automationInterval == 0)
                eq.setBandFrequency(0, static_cast<float>(sweepFrequency(idx / kSr)));
        eq.process(whole.getSubBlock(start, len));
    }
    return out;
}

struct EnvelopeStats {
    float maxJumpDb = 0.0f; // largest RMS change between adjacent 1 ms frames
    int jumpCount = 0;      // number of changes above the threshold
    float peakEnvDb = -100.0f;
    double peakAtMs = 0.0;
};

EnvelopeStats analyzeEnvelope(const AudioBuffer& y) {
    EnvelopeStats s;
    const float* x = y.getReadPointer(0);
    const int frameLen = secondsToSamples(kFrameMs * 0.001, kSr);
    const int frames = y.getNumSamples() / frameLen;
    float prevDb = gainToDb(rms(x, frameLen));
    for (int f = 1; f < frames; ++f) {
        const float db = gainToDb(rms(x + f * frameLen, frameLen));
        const float jump = std::abs(db - prevDb);
        s.maxJumpDb = std::max(s.maxJumpDb, jump);
        if (jump > kJumpThresholdDb)
            ++s.jumpCount;
        if (db > s.peakEnvDb) {
            s.peakEnvDb = db;
            s.peakAtMs = f * kFrameMs;
        }
        prevDb = db;
    }
    return s;
}

void printRow(const char* name, const AudioBuffer& y) {
    const EnvelopeStats s = analyzeEnvelope(y);
    std::printf("  %-11s     %5.2f          %3d          %+6.2f @ %4.0f ms\n", name, s.maxJumpDb, s.jumpCount,
                s.peakEnvDb, s.peakAtMs);
}

// ---- Part 3: switching bands on/off, changing type ----
// The input is a 1 kHz sine (amplitude 0.25). Events (all on block boundaries):
//   block 24 (0.256 s): switch on a LowCut @ 2 kHz (about -12 dB at 1 kHz, with a large phase shift)
//   block 72 (0.768 s): change it to Peak +6 dB @ 1 kHz (type change)
//   block 120 (1.28 s): switch it off
constexpr double kSwitchSeconds = 1.6;
constexpr int kEventBlocks[3] = {24, 72, 120};

EqBand switchEventBand(int event) {
    if (event == 0)
        return {EqBandType::LowCut, 2000.0f, 0.707f, 0.0f, true};
    if (event == 1)
        return {EqBandType::Peak, 1000.0f, 1.0f, 6.0f, true};
    return {EqBandType::Peak, 1000.0f, 1.0f, 6.0f, false};
}

// naive: a single Biquad; when an event occurs, swap the coefficients / bypass directly
AudioBuffer runSwitchNaive(const AudioBuffer& input) {
    AudioBuffer out = input;
    Biquad bq;
    bq.prepare(kSr, kBlockSize, 1);
    bool enabled = false;
    float* d = out.getWritePointer(0);
    const int n = out.getNumSamples();
    for (int start = 0, block = 0; start < n; start += kBlockSize, ++block) {
        for (int e = 0; e < 3; ++e)
            if (block == kEventBlocks[e]) {
                const EqBand band = switchEventBand(e);
                enabled = band.enabled;
                bq.setCoefficients(ParametricEq::makeCoefficients(kSr, band));
            }
        const int len = std::min(kBlockSize, n - start);
        for (int i = start; i < start + len; ++i)
            d[i] = enabled ? bq.processSample(0, d[i]) : d[i];
    }
    return out;
}

// ParametricEq: hand the same events to setBand(), which takes care of the fade-out / switch / fade-in
AudioBuffer runSwitchSmooth(const AudioBuffer& input) {
    AudioBuffer out = input;
    ParametricEq eq;
    eq.prepare(kSr, kBlockSize, 1);
    AudioBlock whole(out);
    const int n = out.getNumSamples();
    for (int start = 0, block = 0; start < n; start += kBlockSize, ++block) {
        for (int e = 0; e < 3; ++e)
            if (block == kEventBlocks[e])
                eq.setBand(0, switchEventBand(e));
        eq.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
    }
    return out;
}

// Maximum of the sine prediction residual: r[n] = y[n] - 2cos(ω)·y[n-1] + y[n-2]
// (looks only at the range [from, to); to < 0 means up to the end)
float maxSineResidual(const AudioBuffer& y, double freq, int from = 2, int to = -1) {
    const float* d = y.getReadPointer(0);
    const double c = 2.0 * std::cos(kTwoPi * freq / kSr);
    if (to < 0 || to > y.getNumSamples())
        to = y.getNumSamples();
    double m = 0.0;
    for (int i = std::max(2, from); i < to; ++i)
        m = std::max(m, std::abs(d[i] - c * d[i - 1] + d[i - 2]));
    return static_cast<float>(m);
}

} // namespace

int main() {
    std::printf("=== Chapter 03 Example 3: sweeping the peak frequency, naive vs smooth vs reference ===\n");
    std::printf("\npeak +%.0f dB Q=%.0f, %.0f Hz -> %.0f Hz, %.0f s; the host sends parameters every %.0f ms\n", kPeakGainDb, kPeakQ,
                kFreqStart, kFreqEnd, kSeconds, kAutomationIntervalSec * 1000.0);

    // ---- 1. Analysis: loudness envelope of a 1 kHz sine ----
    const AudioBuffer sine = makeSine(kToneHz, kSr, kSeconds, 0.25f, 1);
    const AudioBuffer sineRef = runReference(sine);
    const AudioBuffer sineNaive = runNaive(sine);
    const AudioBuffer sineSmooth1 = runSmoothed(sine, 1);
    const AudioBuffer sineSmooth32 = runSmoothed(sine, ParametricEq::kDefaultUpdateInterval);
    const AudioBuffer sineSmooth512 = runSmoothed(sine, kBlockSize);

    std::printf("\n[analysis] 1 kHz sine, RMS computed per %.0f ms frame, looking at the loudness jump between adjacent frames\n", kFrameMs);
    std::printf("  version      max jump(dB/ms)  jumps>%.0f dB     envelope peak\n", double(kJumpThresholdDb));
    printRow("reference", sineRef);
    printRow("naive", sineNaive);
    printRow("smooth/1", sineSmooth1);
    printRow("smooth/32", sineSmooth32);
    printRow("smooth/512", sineSmooth512);
    std::printf("\n  How to read it:\n");
    std::printf("  - reference changes by only a few tenths of a dB per millisecond: this is the envelope change rate that is\n");
    std::printf("    \"musically\" expected when a +12 dB, Q=6 peak sweeps past 1 kHz at 1.33 oct/s.\n");
    std::printf("  - naive jumps by several dB at once (the parameter steps every 100 ms, and so does the loudness): this is zipper noise.\n");
    std::printf("    Its envelope peak is also lower than the reference's -- a step happened to straddle the center of the peak.\n");
    std::printf("  - smooth/1 and smooth/32 are as smooth as the reference, only about one automation interval (roughly 90-100 ms) late in reaching the peak:\n");
    std::printf("    the ramp \"chases\" the target, and this delay is inaudible.\n");
    std::printf("  - smooth/512 changes the coefficients only once per block, so its jumps fall between the two: the update interval is a trade-off between CPU and smoothness.\n");

    // ---- 2. Listening test: sawtooth wah ----
    const AudioBuffer saw = makeSaw(110.0, 30, kSeconds, 0.4f);
    const AudioBuffer sawRef = runReference(saw);
    const AudioBuffer sawNaive = runNaive(saw);
    const AudioBuffer sawSmooth = runSmoothed(saw, ParametricEq::kDefaultUpdateInterval);
    std::printf("\n[listening] 110 Hz sawtooth (30 harmonics): peak(dBFS) reference %+.2f / naive %+.2f / smooth %+.2f\n",
                gainToDb(peakAbs(sawRef)), gainToDb(peakAbs(sawNaive)), gainToDb(peakAbs(sawSmooth)));

    // ---- 3. Switching bands on/off and changing type ----
    const AudioBuffer tone = makeSine(kToneHz, kSr, kSwitchSeconds, 0.25f, 1);
    const AudioBuffer switchNaive = runSwitchNaive(tone);
    const AudioBuffer switchSmooth = runSwitchSmooth(tone);
    const float resDry = maxSineResidual(tone, kToneHz);
    const float resNaive = maxSineResidual(switchNaive, kToneHz);
    const float resSmooth = maxSineResidual(switchSmooth, kToneHz);
    std::printf("\n[on/off / type change] 1 kHz sine (amplitude 0.25): LowCut@2k on at 0.256 s, changed to Peak +6 dB@1k at 0.768 s, off at 1.28 s\n");
    std::printf("  sine prediction residual max|r| (smaller is cleaner; amplitude 0.25 is 0 dB)\n");
    std::printf("  original sine              %.6f  (%+6.1f dB)\n", resDry, gainToDb(resDry / 0.25f));
    std::printf("  naive (direct switching)   %.6f  (%+6.1f dB)\n", resNaive, gainToDb(resNaive / 0.25f));
    std::printf("  ParametricEq (fade out/in) %.6f  (%+6.1f dB)\n", resSmooth, gainToDb(resSmooth / 0.25f));
    std::printf("  max|r| around each event (from 5 ms before to 100 ms after the event):\n");
    std::printf("      time       naive      ParametricEq   event\n");
    const char* eventNames[3] = {"LowCut@2k on", "changed to Peak +6 dB@1k", "off"};
    for (int e = 0; e < 3; ++e) {
        const int at = kEventBlocks[e] * kBlockSize;
        const int from = at - secondsToSamples(0.005, kSr), to = at + secondsToSamples(0.1, kSr);
        std::printf("    %6.3f s   %.6f   %.6f       %s\n", at / kSr, maxSineResidual(switchNaive, kToneHz, from, to),
                    maxSineResidual(switchSmooth, kToneHz, from, to), eventNames[e]);
    }
    std::printf("  -> naive has a sharp residual at every event (= the waveform kinks instantly, an audible click);\n");
    std::printf("     ParametricEq spreads the same change over roughly 20-40 ms, and the residual is more than an order of magnitude smaller.\n");
    writeWav(outputPath("l03_switch_naive.wav"), switchNaive, kSr);
    writeWav(outputPath("l03_switch_smooth.wav"), switchSmooth, kSr);

    writeWav(outputPath("l03_sweep_sine_naive.wav"), sineNaive, kSr);
    writeWav(outputPath("l03_sweep_sine_smooth.wav"), sineSmooth32, kSr);
    writeWav(outputPath("l03_sweep_saw_reference.wav"), sawRef, kSr);
    writeWav(outputPath("l03_sweep_saw_naive.wav"), sawNaive, kSr);
    writeWav(outputPath("l03_sweep_saw_smooth.wav"), sawSmooth, kSr);
    std::printf("\nOutput: dawlab_output/l03_sweep_sine_naive.wav, l03_sweep_sine_smooth.wav,\n"
                "      l03_sweep_saw_reference.wav, l03_sweep_saw_naive.wav, l03_sweep_saw_smooth.wav,\n"
                "      l03_switch_naive.wav, l03_switch_smooth.wav\n");

    // Self-check: the conclusions stated in "How to read it" above must really hold, otherwise ctest reports a failure
    std::printf("\n");
    const EnvelopeStats stNaive = analyzeEnvelope(sineNaive);
    for (const AudioBuffer* y : {&sineSmooth1, &sineSmooth32}) {
        const EnvelopeStats st = analyzeEnvelope(*y);
        DAWLAB_CHECK(st.maxJumpDb < 0.5f); // the smoothed versions change by only a few tenths of a dB per millisecond
        DAWLAB_CHECK(st.jumpCount == 0);
    }
    DAWLAB_CHECK(stNaive.jumpCount > 0); // naive really does jump (the control group must hold for the comparison to mean anything)
    DAWLAB_CHECK(resSmooth < resNaive / 5.0f);
    DAWLAB_CHECK(!hasNanOrInf(sawSmooth));
    return dawlab::test::summary("l03_eq_automation");
}
