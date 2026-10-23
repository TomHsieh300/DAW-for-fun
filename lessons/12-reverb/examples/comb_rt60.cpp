// Example 1: verifying the RT60 of a feedback comb filter
//
// What it demonstrates:
//   1. Compute the comb's feedback with g = 10^(-3*D / (RT60*fs)), feed in an impulse,
//      measure how long the decay to -60 dB actually takes, and compare it with the RT60 we asked for.
//   2. Why a comb is called a comb: the frequency response has peaks (1/(1-g)) at integer multiples
//      of fs/D and notches (1/(1+g)) at half-integer multiples.
//   3. The magnitude response of a Schroeder allpass is flat (0 dB).
//   4. Damping (a lowpass inside the loop) makes the RT60 of high frequencies shorter than that of low frequencies.
//
// How to run: run with no arguments (ctest runs it too).
// Output files (build/dawlab_output/):
//   l12_comb_ir.wav         impulse response of a comb with RT60 = 2 s (a click every 32 ms, each one quieter)
//   l12_comb_damped_ir.wav  the same with damping 0.5 (the clicks get progressively duller)
// What you should hear: a train of evenly spaced, exponentially decaying clicks (like a flutter echo
// from clapping in a hallway); in the damped version the high frequencies disappear first.

#include <dawlab/Common.h>
#include <dawlab/dsp/ReverbBuildingBlocks.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kDelay = 1557; // length of one of the Freeverb combs (32.4 ms @ 48 kHz)

// Feed an impulse block by block and return the whole impulse response (mono)
AudioBuffer impulseResponse(CombFilter& comb, int numSamples) {
    AudioBuffer out = makeImpulse(numSamples, 1);
    comb.reset();
    for (int start = 0; start < numSamples; start += kBlockSize) {
        const int len = std::min(kBlockSize, numSamples - start);
        float* d = out.getWritePointer(0) + start;
        for (int i = 0; i < len; ++i)
            d[i] = comb.processSample(d[i]);
    }
    return out;
}

// Amplitude ratio of one frequency component across two time windows -> converted to the RT60 at that frequency
double rt60AtFrequency(const AudioBuffer& ir, double freq, double t1, double t2, double windowSec) {
    const int w = secondsToSamples(windowSec, kSampleRate);
    const double a1 = goertzelAmplitude(ir.getReadPointer(0) + secondsToSamples(t1, kSampleRate), w, freq, kSampleRate);
    const double a2 = goertzelAmplitude(ir.getReadPointer(0) + secondsToSamples(t2, kSampleRate), w, freq, kSampleRate);
    const double slope = 20.0 * std::log10(a2 / a1) / (t2 - t1); // dB / s
    return slope < 0.0 ? -60.0 / slope : 0.0;
}

} // namespace

int main() {
    std::printf("=== Chapter 12 Example 1: verifying the RT60 of a comb filter ===\n");
    std::printf("fs = %.0f Hz, D = %d samples (%.2f ms), fundamental fs/D = %.2f Hz\n\n", kSampleRate, kDelay,
                samplesToMs(kDelay, kSampleRate), kSampleRate / kDelay);

    CombFilter comb;
    comb.prepare(kDelay);
    comb.setDelaySamples(kDelay);

    // ---- 1. Requested RT60 vs measured value ---------------------------------------
    std::printf("--- RT60 verification (the k-th echo of the impulse response = g^(k-1)) ---\n");
    std::printf("%8s %10s %12s %14s %14s\n", "RT60(s)", "g", "rounds k-1", "-60dB time(s)", "regression(s)");
    for (double rt60 : {0.5, 1.0, 2.0, 4.0}) {
        const float g = CombFilter::feedbackForRt60(kDelay, rt60, kSampleRate);
        comb.setFeedback(g);
        comb.setDamping(0.0f);
        const int n = secondsToSamples(rt60 * 1.3 + 0.2, kSampleRate);
        AudioBuffer ir = impulseResponse(comb, n);

        // The first echo is at n = D (amplitude 1); the k-th echo is at n = kD (amplitude g^(k-1)).
        // Find the first k with |echo| <= 10^(-60/20). The decay time is counted from the first echo
        // (the 0 dB reference point): (k-1)*D / fs, i.e. k-1 trips around the loop. (The absolute time of the
        // k-th echo is k*D/fs, which includes an extra D/fs of propagation delay for the first echo itself.)
        int k = 1;
        while (k * kDelay < n && std::abs(ir.getSample(0, k * kDelay)) > 1e-3f)
            ++k;
        const double measured = (k - 1) * static_cast<double>(kDelay) / kSampleRate;
        // Theory: g^(k-1) <= 1e-3 -> rounds k-1 = ceil(3 / -log10(g)) = ceil(RT60*fs/D)
        const int expectedRounds = static_cast<int>(std::ceil(rt60 * kSampleRate / kDelay));
        // Another way to measure: linear regression on a windowed-RMS envelope
        // (the window must be longer than D, otherwise it only sees zeros)
        const DecayMeasurement m = measureRt60(ir.getReadPointer(0), n, kSampleRate, 40.0);
        std::printf("%8.2f %10.5f %12d %14.3f %14.3f\n", rt60, g, expectedRounds, measured, m.rt60Seconds);

        if (rt60 == 2.0)
            writeWav(outputPath("l12_comb_ir.wav"), ir, kSampleRate);
    }
    std::printf("(The decay time is counted from the first echo = rounds x D/fs, so it is quantized to integer "
                "multiples of D/fs = %.1f ms and comes out slightly above the requested value)\n\n",
                samplesToMs(kDelay, kSampleRate));

    // ---- 2. Frequency response: peaks and notches ----------------------------------
    {
        const float g = CombFilter::feedbackForRt60(kDelay, 2.0, kSampleRate);
        comb.setFeedback(g);
        comb.setDamping(0.0f);
        const double f0 = kSampleRate / kDelay;
        std::printf("--- Frequency response (g = %.4f) ---\n", g);
        std::printf("Theoretical peak = 1/(1-g) = %+.2f dB, notch = 1/(1+g) = %+.2f dB\n", gainToDb(1.0 / (1.0 - g)),
                    gainToDb(1.0 / (1.0 + g)));
        for (double m : {10.0, 10.5, 40.0, 40.5}) {
            comb.reset();
            const double gainDb =
                measureGainDb([&](float x) { return comb.processSample(x); }, m * f0, kSampleRate, 1.0, 3.0);
            std::printf("  f = %4.1f x fs/D = %8.2f Hz : %+7.2f dB  (%s)\n", m, m * f0, gainDb,
                        std::fmod(m, 1.0) == 0.0 ? "peak" : "notch");
        }
        std::printf("\n");
    }

    // ---- 3. Allpass: perfectly flat magnitude --------------------------------------
    {
        AllpassDiffuser ap;
        ap.prepare(556);
        ap.setDelaySamples(556);
        ap.setGain(0.5f);
        std::printf("--- Magnitude response of a Schroeder allpass (D = 556, g = 0.5) ---\n");
        for (double f : {100.0, 1000.0, 5000.0, 12000.0}) {
            ap.reset();
            const double gainDb = measureGainDb([&](float x) { return ap.processSample(x); }, f, kSampleRate, 0.5, 0.5);
            std::printf("  %8.0f Hz : %+6.2f dB\n", f, gainDb);
        }
        std::printf("\n");
    }

    // ---- 4. Damping: high frequencies decay faster ---------------------------------
    {
        const float g = CombFilter::feedbackForRt60(kDelay, 2.0, kSampleRate);
        const int n = secondsToSamples(2.6, kSampleRate);
        const double f0 = kSampleRate / kDelay;
        std::printf("--- Effect of damping on the RT60 at each frequency (two 0.2 s windows, at 0.1 s and 0.9 s) ---\n");
        std::printf("%10s %12s %12s\n", "freq(Hz)", "damp=0", "damp=0.5");
        comb.setFeedback(g);
        comb.setDamping(0.0f);
        AudioBuffer dry = impulseResponse(comb, n);
        comb.setDamping(0.5f);
        AudioBuffer damped = impulseResponse(comb, n);
        writeWav(outputPath("l12_comb_damped_ir.wav"), damped, kSampleRate);
        for (double target : {250.0, 1000.0, 4000.0, 8000.0}) {
            const double f = std::round(target / f0) * f0; // snap to the nearest comb harmonic so we avoid landing in a notch
            std::printf("%10.1f %12.3f %12.3f\n", f, rt60AtFrequency(dry, f, 0.1, 0.9, 0.2),
                        rt60AtFrequency(damped, f, 0.1, 0.9, 0.2));
        }
        std::printf("(Without damping every frequency has the same RT60; with damping the high frequencies get clearly shorter)\n");
    }

    return 0;
}
