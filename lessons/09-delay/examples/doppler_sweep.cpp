// Example 3: sweeping the delay time -- the Doppler (pitch shift) effect, and clicks
//
// What it demonstrates:
//   1. When the delay time changes continuously, the read pointer "chases" or "flees" the
//      write pointer and the output pitch shifts:
//        f_out = f_in × (1 − dτ/dt)        τ = delay time (seconds)
//      A delay that grows linearly from 10 ms to 250 ms over 2 seconds gives dτ/dt = 0.12, so
//      440 Hz becomes 387.2 Hz; sweeping back (250 → 10 ms) gives 492.8 Hz. This example
//      measures the actual frequency with zero crossings to verify it.
//   2. If the delay time is not smoothed and jumps straight to the new value, the waveform
//      gets a discontinuity (a click). We measure the largest difference between adjacent
//      samples: a 440 Hz sine of amplitude 0.5 has a steepest slope of only 0.029 / sample,
//      while the abrupt version shows a step far larger than that (about 0.27 here; the size
//      depends on how different the waveform is at the old and new read positions at the
//      moment of the switch).
//   3. Self-check: a very long delay changed by a tiny amount (1900 → 1901 ms, 15000 → 15001 ms)
//      must not click either. With the read position around 90 thousand or 720 thousand
//      samples, float does not have enough precision (ulp = 1/128, 1/16 sample), so the
//      Delay's delay-time ramp is computed in double (see DelayRamp in Delay.h).
//      This part is verified with DAWLAB_CHECK: during the ramp, the largest difference
//      between adjacent samples must be < 1.5 × the sine's own slope limit.
//
// How to run: no arguments needed, just run l09_doppler_sweep.
// Output files (dawlab_output/):
//   l09_doppler_smooth.wav   440 Hz sine, delay time swept smoothly (pitch slides down, then up)
//   l09_doppler_abrupt.wav   the same changes but with ramp = 0 (two clicks, pitch unchanged)
// What you should hear: the smooth version sounds like a tape machine changing speed; the
//             abrupt version is a "click".

#include <dawlab/Common.h>
#include <dawlab/dsp/Delay.h>

#include <cmath>
#include <cstdio>

using dawlab::dsp::Delay;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kToneHz = 440.0;
constexpr float kStartMs = 10.0f;
constexpr float kEndMs = 250.0f;
constexpr double kRampSeconds = 2.0;

// Estimate a signal's frequency from its rising zero crossings: f ~ (crossings - 1) / (last - first)
double estimateFrequency(const float* x, int n) {
    int first = -1, last = -1, count = 0;
    for (int i = 1; i < n; ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            if (first < 0)
                first = i;
            last = i;
            ++count;
        }
    }
    if (count < 2)
        return 0.0;
    return (count - 1) * kSampleRate / static_cast<double>(last - first);
}

float maxSampleStep(const float* x, int n) {
    float m = 0.0f;
    for (int i = 1; i < n; ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

// Render the whole thing: set the delay to kEndMs at t = 1 s and back to kStartMs at t = 4 s
// (splitting blocks at the event points).
dawlab::AudioBuffer render(double rampSeconds) {
    dawlab::AudioBuffer buffer = dawlab::makeSine(kToneHz, kSampleRate, 7.0, 0.5f, 1);
    Delay delay;
    delay.setMaxDelayMs(1000.0f);
    delay.prepare(kSampleRate, kBlockSize, 1);
    delay.setDelayRampSeconds(rampSeconds);
    delay.setDelayMs(kStartMs);
    delay.setFeedback(0.0f);
    delay.setMix(1.0f); // wet only, so the pure pitch change is visible
    delay.reset();

    const int upAt = dawlab::secondsToSamples(1.0, kSampleRate);
    const int downAt = dawlab::secondsToSamples(4.0, kSampleRate);
    dawlab::AudioBlock whole(buffer);
    const int total = buffer.getNumSamples();
    // 48000 is not a multiple of 512: a parameter change can land in the middle of a block.
    // We do what a host does for automation -- split the block at the event point, using the
    // old parameters for the first part and the new ones for the second.
    int start = 0;
    while (start < total) {
        if (start == upAt)
            delay.setDelayMs(kEndMs);
        if (start == downAt)
            delay.setDelayMs(kStartMs);
        int n = std::min(kBlockSize, total - start);
        for (int event : {upAt, downAt})
            if (start < event && event < start + n)
                n = event - start; // cut the block at the event point
        delay.process(whole.getSubBlock(start, n));
        start += n;
    }
    return buffer;
}

// A small change to a long delay: hold the delay at baseMs, then after a while change it to
// baseMs + 1 (default 50 ms ramp). Returns the largest difference between adjacent samples
// from 0.05 s before the change to 0.15 s after it (the ramp itself lasts only 0.05 s).
float longDelayNudgeMaxStep(float baseMs, float maxDelayMs) {
    Delay delay;
    delay.setMaxDelayMs(maxDelayMs);
    delay.prepare(kSampleRate, kBlockSize, 1);
    delay.setDelayMs(baseMs);
    delay.setFeedback(0.0f);
    delay.setMix(1.0f);
    delay.reset();

    // Wait for the delay line to fill with sine (the wet signal is silent for the first
    // baseMs), then let another 100 blocks go by before changing the value.
    const int changeAt = dawlab::msToSamples(static_cast<double>(baseMs), kSampleRate) + kBlockSize * 100;
    const int total = changeAt + dawlab::secondsToSamples(0.5, kSampleRate);
    dawlab::AudioBuffer buffer = dawlab::makeSine(kToneHz, kSampleRate, total / kSampleRate + 0.01, 0.5f, 1);
    dawlab::AudioBlock whole(buffer);
    int start = 0;
    while (start < total) {
        if (start == changeAt)
            delay.setDelayMs(baseMs + 1.0f);
        int n = std::min(kBlockSize, total - start);
        if (start < changeAt && changeAt < start + n)
            n = changeAt - start; // split the block at the event point
        delay.process(whole.getSubBlock(start, n));
        start += n;
    }
    const int from = changeAt - dawlab::secondsToSamples(0.05, kSampleRate);
    return maxSampleStep(buffer.getReadPointer(0) + from, dawlab::secondsToSamples(0.2, kSampleRate));
}

} // namespace

int main() {
    std::printf("=== Chapter 09, Example 3: Doppler effect from sweeping the delay time ===\n\n");

    const double slope = (kEndMs - kStartMs) * 0.001 / kRampSeconds; // dτ/dt (dimensionless)
    const double expectDown = kToneHz * (1.0 - slope);
    const double expectUp = kToneHz * (1.0 + slope);
    std::printf("delay %g ms -> %g ms over %.1f s: d(tau)/dt = %.3f\n", kStartMs, kEndMs, kRampSeconds, slope);
    std::printf("Theory: sweeping up gives f_out = %.1f x (1 - %.3f) = %.1f Hz; sweeping back gives %.1f Hz\n\n",
                kToneHz, slope, expectDown, expectUp);

    // Note: the delay changes at 1 s and 4 s, each taking kRampSeconds to reach the new value.
    dawlab::AudioBuffer smooth = render(kRampSeconds);
    dawlab::AudioBuffer abrupt = render(0.0);

    auto window = [&](const dawlab::AudioBuffer& b, double t0, double t1) {
        const int s = dawlab::secondsToSamples(t0, kSampleRate);
        const int n = dawlab::secondsToSamples(t1 - t0, kSampleRate);
        return estimateFrequency(b.getReadPointer(0) + s, n);
    };
    std::printf("Measured frequency of the smooth version (zero crossings):\n");
    std::printf("  0.3-0.8 s (delay fixed at %g ms)   : %.1f Hz (expected %.1f)\n", kStartMs, window(smooth, 0.3, 0.8),
                kToneHz);
    std::printf("  1.5-2.5 s (delay sweeping up)      : %.1f Hz (expected %.1f)\n", window(smooth, 1.5, 2.5), expectDown);
    std::printf("  3.2-3.8 s (delay fixed at %g ms)  : %.1f Hz (expected %.1f)\n", kEndMs, window(smooth, 3.2, 3.8),
                kToneHz);
    std::printf("  4.5-5.5 s (delay sweeping back)    : %.1f Hz (expected %.1f)\n\n", window(smooth, 4.5, 5.5), expectUp);

    const double maxSlope = 0.5 * dawlab::kTwoPi * kToneHz / kSampleRate; // the sine's largest |Δy|
    auto stepAround = [&](const dawlab::AudioBuffer& b, double t) {
        const int s = dawlab::secondsToSamples(t, kSampleRate) - 2; // start looking 2 samples before the change
        return maxSampleStep(b.getReadPointer(0) + s, dawlab::secondsToSamples(0.1, kSampleRate));
    };
    std::printf("Largest difference between adjacent samples (the sine's own limit = %.4f):\n", maxSlope);
    std::printf("  smooth version 1.0-1.1 s: %.4f   4.0-4.1 s: %.4f (pitch rises %.0f%% on the way back, so the limit becomes %.4f)\n",
                stepAround(smooth, 1.0), stepAround(smooth, 4.0), slope * 100.0, maxSlope * (1.0 + slope));
    std::printf("  abrupt version 1.0-1.1 s: %.4f   4.0-4.1 s: %.4f   <- far above the limit = click\n\n",
                stepAround(abrupt, 1.0), stepAround(abrupt, 4.0));

    // ---- 3. Small change to a long delay: must not click because of float precision ----
    // Changing by 1 ms with a 50 ms ramp -> d(tau)/dt = 0.02, so the sine's slope limit only grows to 1.02x.
    const float step1900 = longDelayNudgeMaxStep(1900.0f, 2000.0f);
    const float step15000 = longDelayNudgeMaxStep(15000.0f, 16000.0f);
    std::printf("Largest adjacent-sample difference for a 1 ms change to a long delay (50 ms ramp; limit %.4f x 1.02 = %.4f):\n", maxSlope,
                maxSlope * 1.02);
    std::printf("  1900 -> 1901 ms  : %.4f\n", step1900);
    std::printf("  15000 -> 15001 ms: %.4f\n", step15000);
    std::printf("  (If the ramp accumulated in float sample by sample, the read-position error would build up and jump back to the target when the ramp ends = click.)\n\n");
    DAWLAB_CHECK(step1900 < 1.5 * maxSlope);
    DAWLAB_CHECK(step15000 < 1.5 * maxSlope);
    // Also verify the conclusion of part 2: the smooth version has no discontinuity, the abrupt one does.
    DAWLAB_CHECK(stepAround(smooth, 1.0) < 1.1 * maxSlope);
    DAWLAB_CHECK(stepAround(abrupt, 1.0) > 5.0 * maxSlope);
    {
        // Safety net: calling reset() / process() before prepare() must not crash (a host may
        // call reset() once before prepare()).
        Delay unprepared;
        unprepared.reset();
        dawlab::AudioBuffer tiny(1, 16);
        tiny.getWritePointer(0)[0] = 1.0f;
        unprepared.process(dawlab::AudioBlock(tiny));
        DAWLAB_CHECK(tiny.getReadPointer(0)[0] == 1.0f); // not prepared: passed through untouched
    }

    dawlab::writeWav(dawlab::outputPath("l09_doppler_smooth.wav"), smooth, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l09_doppler_abrupt.wav"), abrupt, kSampleRate);
    std::printf("Wrote dawlab_output/l09_doppler_smooth.wav, l09_doppler_abrupt.wav\n\n");
    return dawlab::test::summary("l09_doppler_sweep");
}
