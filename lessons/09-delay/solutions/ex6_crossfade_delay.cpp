// Exercise 6 solution: switching the delay time with a crossfade (no pitch shift, no click)
//
// The chapter 09 Delay uses the "smooth transition" strategy: the delay time ramps linearly
// to the new value, and during the ramp the read pointer does not move at the same speed as
// the write pointer -> a Doppler pitch shift (example 3). That is a feature in modulation
// effects, but a DAW user switching "1/8 -> 1/4" usually does not want to hear a tape machine
// changing speed.
//
// The other strategy: two read heads (A and B).
//   - Normally only A is used.
//   - When a new delay time arrives, put B at the new position (its delay is fixed, so there
//     is no Doppler), then within crossfadeMs fade the output out of A and into B
//     (raised cosine: gB = ½(1 − cos πt), gA = 1 − gB).
//   - When the fade finishes, swap roles: B becomes A.
//   - If another new value arrives during the crossfade -> remember it, and do the next
//     fade after this one completes.
//
//   x ──►(+)──► delay line ──┬── head A (D_old) ──×gA──┐
//         ▲                  │                          (+)──► wet
//         │                  └── head B (D_new) ──×gB──┘
//         └──────────── ×fb ◄───────────────────────────┘
//
// Self-checks (440 Hz sine, 10 ms -> 250 ms):
//   1. During the crossfade, the largest difference between adjacent samples does not exceed
//      the sine's own slope by much (no click);
//   2. The frequency measured by zero crossings over ±50 ms around the crossfade is within
//      440 ± 6 Hz (no Doppler; the few Hz that remain are a phase slip caused by the two
//      read heads being out of phase, see solutions/README.md);
//   3. After the fade ends, the output is exactly x[n − 12000];
//   4. Controls: crossfade = 0 (jump straight) shows a discontinuity far larger than the
//      sine's slope; the frequency measured from Delay (smooth transition, 20 ms ramp)
//      deviates badly from 440 Hz;
//   5. Answer to the question: the amplitude at the midpoint of the fade = |cos(φ/2)| = 0.309;
//      white noise (the two heads are uncorrelated) through CrossfadeDelay is 3 dB quieter
//      at the midpoint of the fade. The equal-power midpoint is 0 dB (+3 dB for correlated
//      material); this is only printed as a formula comparison and is not a check.
//
// Run: l09_ex6_solution. Writes dawlab_output/l09_ex6_crossfade.wav, l09_ex6_abrupt.wav, l09_ex6_ramp.wav.

#include <dawlab/Common.h>
#include <dawlab/dsp/Delay.h>
#include <dawlab/dsp/FractionalDelayLine.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using dawlab::dsp::Delay;
using dawlab::dsp::FractionalDelayLine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kToneHz = 440.0;
constexpr float kDelayA = 10.0f;
constexpr float kDelayB = 250.0f;
constexpr double kSwitchAt = 1.0; // seconds

class CrossfadeDelay {
public:
    static constexpr int kMaxChannels = 2;

    void setMaxDelayMs(float ms) noexcept { maxDelayMs_ = std::max(1.0f, ms); }

    void prepare(double sampleRate, int /*maxBlockSize*/, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = std::clamp(numChannels, 1, kMaxChannels);
        maxDelaySamples_ = dawlab::msToSamples(maxDelayMs_, sampleRate) + 2;
        line_.prepare(maxDelaySamples_, numChannels_);
        prepared_ = true;
        setCrossfadeMs(crossfadeMs_);
        reset();
    }

    void reset() noexcept {
        line_.reset();
        // A reset does not need a crossfade (the buffer has been cleared): both read heads are
        // aligned straight to the current target. Otherwise the order prepare → setDelayMs →
        // reset would throw away a fade that has not finished and leave us stuck at the old delay.
        if (prepared_)
            headDelay_[0] = headDelay_[1] = toSamples(delayMs_);
        active_ = 0;
        fadePos_ = -1;
        pending_ = false;
    }

    // Delay time change: not a ramp, but put the other read head at the new position and start a crossfade
    void setDelayMs(float ms) noexcept {
        delayMs_ = std::clamp(ms, 0.0f, maxDelayMs_);
        if (!prepared_)
            return;
        if (fadePos_ >= 0) { // already fading: note it down and handle it once the fade is done
            pending_ = true;
            return;
        }
        startFade();
    }

    void setCrossfadeMs(float ms) noexcept {
        crossfadeMs_ = std::max(0.0f, ms);
        if (prepared_)
            fadeLength_ = std::max(1, dawlab::msToSamples(crossfadeMs_, sampleRate_));
    }
    void setFeedback(float fb) noexcept { feedback_ = std::clamp(fb, 0.0f, 0.99f); }
    void setMix(float mix) noexcept { mix_ = std::clamp(mix, 0.0f, 1.0f); }
    bool isFading() const noexcept { return fadePos_ >= 0; }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            // The crossfade weights for this sample (shared by all channels)
            float gA = 1.0f, gB = 0.0f;
            if (fadePos_ >= 0) {
                const float t = static_cast<float>(fadePos_) / static_cast<float>(fadeLength_);
                gB = 0.5f * (1.0f - std::cos(static_cast<float>(dawlab::kPi) * t)); // raised cosine
                gA = 1.0f - gB;
            }
            const int other = 1 - active_;
            for (int ch = 0; ch < nch; ++ch) {
                float* data = block.getChannelPointer(ch);
                const float x = data[i];
                const float a = line_.readCubic(ch, headDelay_[active_] - 1.0f);
                const float wet = fadePos_ >= 0 ? gA * a + gB * line_.readCubic(ch, headDelay_[other] - 1.0f) : a;
                line_.push(ch, x + feedback_ * wet);
                data[i] = (1.0f - mix_) * x + mix_ * wet;
            }
            if (fadePos_ >= 0 && ++fadePos_ >= fadeLength_) { // fade finished: swap the read heads
                active_ = other;
                fadePos_ = -1;
                if (pending_) {
                    pending_ = false;
                    if (toSamples(delayMs_) != headDelay_[active_])
                        startFade();
                }
            }
        }
    }

private:
    float toSamples(float ms) const noexcept {
        return std::clamp(static_cast<float>(ms * 0.001 * sampleRate_), 2.0f, static_cast<float>(maxDelaySamples_));
    }

    void startFade() noexcept {
        const float target = toSamples(delayMs_);
        if (target == headDelay_[active_])
            return;
        if (fadeLength_ <= 1 || crossfadeMs_ <= 0.0f) { // crossfade = 0: jump straight (clicks; for comparison)
            headDelay_[active_] = target;
            return;
        }
        headDelay_[1 - active_] = target;
        fadePos_ = 0;
    }

    FractionalDelayLine line_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int maxDelaySamples_ = 0;
    float maxDelayMs_ = 2000.0f;
    float delayMs_ = 250.0f;
    float crossfadeMs_ = 20.0f;
    float feedback_ = 0.0f;
    float mix_ = 1.0f;
    float headDelay_[2] = {2.0f, 2.0f};
    int active_ = 0;
    int fadePos_ = -1; // -1 = not fading
    int fadeLength_ = 1;
    bool pending_ = false;
    bool prepared_ = false;
};

// Estimate the frequency from rising zero crossings
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
    return count < 2 ? 0.0 : (count - 1) * kSampleRate / static_cast<double>(last - first);
}

float maxSampleStep(const float* x, int n) {
    float m = 0.0f;
    for (int i = 1; i < n; ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

dawlab::AudioBuffer makeTone() { return dawlab::makeSine(kToneHz, kSampleRate, 3.0, 0.5f, 1); }

// Both kinds of delay switch from kDelayA to kDelayB at t = kSwitchAt.
// 48000 is not a multiple of 512, so that block is split at the switch point (as a host does with automation).
template <typename Proc, typename Setter>
void renderInto(Proc& proc, Setter&& setDelay, dawlab::AudioBuffer& b) {
    const int switchAt = dawlab::secondsToSamples(kSwitchAt, kSampleRate);
    const int total = b.getNumSamples();
    dawlab::AudioBlock whole(b);
    int start = 0;
    while (start < total) {
        if (start == switchAt)
            setDelay(kDelayB);
        int n = std::min(kBlockSize, total - start);
        if (start < switchAt && switchAt < start + n)
            n = switchAt - start;
        proc.process(whole.getSubBlock(start, n));
        start += n;
    }
}

template <typename Proc, typename Setter>
dawlab::AudioBuffer render(Proc& proc, Setter&& setDelay) {
    dawlab::AudioBuffer b = makeTone();
    renderInto(proc, setDelay, b);
    return b;
}

dawlab::AudioBuffer renderCrossfade(float crossfadeMs) {
    CrossfadeDelay d;
    d.setMaxDelayMs(1000.0f);
    d.prepare(kSampleRate, kBlockSize, 1);
    d.setCrossfadeMs(crossfadeMs);
    d.setDelayMs(kDelayA);
    d.setFeedback(0.0f);
    d.setMix(1.0f);
    d.reset();
    return render(d, [&](float ms) { d.setDelayMs(ms); });
}

// White noise through CrossfadeDelay (10 -> 250 ms, 1 second crossfade): the output energy
// within ±20 ms of the fade midpoint, relative to the input, in dB.
// The two read heads read noise 240 ms apart (uncorrelated), and at the midpoint gA = gB = 0.5
// -> energy 0.5² + 0.5² = 0.5 -> −3.01 dB.
double measureMidFadeNoiseDb() {
    const float fadeMs = 1000.0f;
    const int switchAt = dawlab::secondsToSamples(kSwitchAt, kSampleRate);
    const int mid = switchAt + dawlab::msToSamples(fadeMs, kSampleRate) / 2;
    const int half = dawlab::msToSamples(20.0, kSampleRate);
    double outEnergy = 0.0, inEnergy = 0.0;
    long long outCount = 0, inCount = 0;
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        CrossfadeDelay d;
        d.setMaxDelayMs(1000.0f);
        d.prepare(kSampleRate, kBlockSize, 1);
        d.setCrossfadeMs(fadeMs);
        d.setDelayMs(kDelayA);
        d.setMix(1.0f);
        d.reset();
        dawlab::AudioBuffer b = dawlab::makeWhiteNoise(kSampleRate, 2.5, 0.25f, 1, seed);
        const dawlab::AudioBuffer input = b;
        renderInto(d, [&](float ms) { d.setDelayMs(ms); }, b);
        for (int n = mid - half; n < mid + half; ++n, ++outCount)
            outEnergy += static_cast<double>(b.getSample(0, n)) * b.getSample(0, n);
        for (int n = 0; n < input.getNumSamples(); ++n, ++inCount)
            inEnergy += static_cast<double>(input.getSample(0, n)) * input.getSample(0, n);
    }
    return 10.0 * std::log10((outEnergy / static_cast<double>(outCount)) / (inEnergy / static_cast<double>(inCount)));
}

dawlab::AudioBuffer renderRamp(double rampSeconds) {
    Delay d;
    d.setMaxDelayMs(1000.0f);
    d.prepare(kSampleRate, kBlockSize, 1);
    d.setDelayRampSeconds(rampSeconds);
    d.setDelayMs(kDelayA);
    d.setFeedback(0.0f);
    d.setMix(1.0f);
    d.reset();
    return render(d, [&](float ms) { d.setDelayMs(ms); });
}

} // namespace

int main() {
    std::printf("=== Chapter 09, Exercise 6: switching the delay time with a crossfade ===\n\n");
    const float crossfadeMs = 20.0f;
    const int switchAt = dawlab::secondsToSamples(kSwitchAt, kSampleRate);
    const int winLen = dawlab::secondsToSamples(0.1, kSampleRate);
    const double sineMaxStep = 0.5 * dawlab::kTwoPi * kToneHz / kSampleRate; // the sine's largest change per sample

    dawlab::AudioBuffer crossfade = renderCrossfade(crossfadeMs);
    dawlab::AudioBuffer abrupt = renderCrossfade(0.0f);
    dawlab::AudioBuffer ramp = renderRamp(0.02);
    dawlab::AudioBuffer tone = makeTone();

    std::printf("440 Hz sine, delay switched from %g ms to %g ms at t = %.1f s\n", kDelayA, kDelayB, kSwitchAt);
    std::printf("Largest difference between adjacent samples of the sine itself: %.4f\n\n", sineMaxStep);

    // ---- 1. No click -----------------------------------------------------------
    const float stepXf = maxSampleStep(crossfade.getReadPointer(0) + switchAt - 2, winLen);
    const float stepAb = maxSampleStep(abrupt.getReadPointer(0) + switchAt - 2, winLen);
    std::printf("Largest adjacent-sample difference within 100 ms after the switch: crossfade %.4f, direct jump %.4f\n", stepXf, stepAb);
    DAWLAB_CHECK(stepXf < 1.5 * sineMaxStep); // crossfade: about the same as the sine itself
    DAWLAB_CHECK(stepAb > 3.0 * sineMaxStep); // direct jump: a discontinuity = click

    // ---- 2. No Doppler ---------------------------------------------------------
    auto freqIn = [&](const dawlab::AudioBuffer& b, double t0, double t1) {
        const int s = dawlab::secondsToSamples(t0, kSampleRate);
        return estimateFrequency(b.getReadPointer(0) + s, dawlab::secondsToSamples(t1 - t0, kSampleRate));
    };
    const double fXf = freqIn(crossfade, kSwitchAt - 0.05, kSwitchAt + 0.05);
    const double fRamp = freqIn(ramp, kSwitchAt, kSwitchAt + 0.02);
    const double rampSlope = (kDelayB - kDelayA) * 0.001 / 0.02; // dτ/dt
    std::printf("Frequency measured over ±50 ms around the switch: crossfade %.1f Hz (expected %.0f)\n", fXf, kToneHz);
    std::printf("Delay smooth transition (20 ms ramp), measured during the ramp: %.1f Hz (d(tau)/dt = %.0f -> theoretical |f_out| = %.0f Hz, "
                "a negative sign means reverse playback)\n",
                fRamp, rampSlope, std::abs(kToneHz * (1.0 - rampSlope)));
    // A few Hz of error is tolerated: the two read heads are out of phase (240 ms × 440 Hz =
    // 105.6 periods -> 0.6 period apart), and during the crossfade the phase of the combined
    // signal "slips" from A to B within 20 ms (at most half a period); spread over a 100 ms
    // measurement window that is about ±5 Hz. Compared with 4840 Hz for the ramp version, this
    // is effectively "no Doppler".
    DAWLAB_CHECK_NEAR(fXf, kToneHz, 6.0);
    DAWLAB_CHECK(std::abs(fRamp - kToneHz) > 100.0); // with such a fast change the smooth transition ruins the pitch completely

    // ---- 3. After the fade ends = pure x[n − D_new] ----------------------------------
    {
        const int dNew = dawlab::msToSamples(kDelayB, kSampleRate); // 12000
        const int from = switchAt + dawlab::msToSamples(crossfadeMs, kSampleRate) + 8;
        float maxErr = 0.0f;
        for (int n = from; n < crossfade.getNumSamples(); ++n)
            maxErr = std::max(maxErr, std::abs(crossfade.getSample(0, n) - tone.getSample(0, n - dNew)));
        std::printf("Largest error between y[n] and x[n - %d] after the fade ends: %.2e\n", dNew, maxErr);
        DAWLAB_CHECK(maxErr < 1.0e-5f);
        // Before the switch it is x[n − 480]
        const int dOld = dawlab::msToSamples(kDelayA, kSampleRate);
        float maxErrBefore = 0.0f;
        for (int n = dOld + 8; n < switchAt; ++n)
            maxErrBefore = std::max(maxErrBefore, std::abs(crossfade.getSample(0, n) - tone.getSample(0, n - dOld)));
        DAWLAB_CHECK(maxErrBefore < 1.0e-5f);
    }

    // ---- 4. A second change arriving during the crossfade: it must be queued, not lost ----
    {
        CrossfadeDelay d;
        d.setMaxDelayMs(1000.0f);
        d.prepare(kSampleRate, kBlockSize, 1);
        d.setCrossfadeMs(20.0f);
        d.setDelayMs(10.0f);
        d.setMix(1.0f);
        d.reset();
        dawlab::AudioBuffer b = dawlab::makeSine(kToneHz, kSampleRate, 1.0, 0.5f, 1);
        dawlab::AudioBlock whole(b);
        d.process(whole.getSubBlock(0, 4800)); // 100 ms warm-up
        d.setDelayMs(100.0f);                  // start the fade
        d.process(whole.getSubBlock(4800, 256));
        DAWLAB_CHECK(d.isFading());
        d.setDelayMs(300.0f); // change again mid-fade: queued
        d.process(whole.getSubBlock(5056, 4800 * 3));
        DAWLAB_CHECK(!d.isFading());
        // It should end up at 300 ms
        const int d300 = dawlab::msToSamples(300.0, kSampleRate);
        float err = 0.0f;
        for (int n = 4800 * 4; n < b.getNumSamples(); ++n) {
            const float expect = static_cast<float>(0.5 * std::sin(dawlab::kTwoPi * kToneHz * (n - d300) / kSampleRate));
            err = std::max(err, std::abs(b.getSample(0, n) - expect));
        }
        std::printf("Changing the delay again mid-fade (100 -> 300 ms): it finally settles at 300 ms, error %.2e\n", err);
        DAWLAB_CHECK(err < 1.0e-4f);
        DAWLAB_CHECK(maxSampleStep(b.getReadPointer(0) + 4800, 4800 * 3) < 1.5 * sineMaxStep);
    }

    // ---- 5. Answers to the questions: amplitude at the fade midpoint, uncorrelated material vs equal-power ----
    {
        // The two heads are 240 ms = 105.6 periods apart -> phase difference φ = 0.6 period; at the
        // midpoint gA = gB = 0.5, so the combined amplitude = 0.5·|1 + e^{jφ}| = |cos(φ/2)|
        // (relative to the original amplitude)
        const double phi = dawlab::kTwoPi * kToneHz * (kDelayB - kDelayA) * 0.001;
        const double expectRatio = std::abs(std::cos(phi / 2.0));
        const int mid = switchAt + dawlab::msToSamples(crossfadeMs, kSampleRate) / 2;
        // A window of half a period is sure to contain an extremum of |sin|; the shorter the
        // window, the less gA and gB change within it (about ±0.02)
        const int halfPeriod = static_cast<int>(kSampleRate / kToneHz / 2.0) + 1;
        const float midPeak = dawlab::peakAbs(crossfade.getReadPointer(0) + mid - halfPeriod / 2, halfPeriod);
        std::printf("\nPeak within half a period at the fade midpoint (gA = gB = 0.5): %.4f, %.3f of the original amplitude (theory |cos(phi/2)| = %.3f, %.1f dB)\n",
                    midPeak, midPeak / 0.5, expectRatio, dawlab::gainToDb(expectRatio));
        DAWLAB_CHECK_NEAR(midPeak / 0.5, expectRatio, 0.01);

        // Real material: the two heads read uncorrelated content. White noise is uncorrelated
        // with itself at different delays, so feed white noise into the "real" CrossfadeDelay
        // (10 -> 250 ms) and measure the output RMS at the fade midpoint. To keep gA and gB
        // almost fixed at 0.5 inside the measurement window, the crossfade is stretched to 1
        // second and only ±20 ms around the midpoint is examined; averaging over 8 different
        // seeds reduces the statistical error of the white-noise RMS itself.
        const double midFadeDb = measureMidFadeNoiseDb();
        std::printf("Level at the crossfade midpoint (relative to the input, measured on CrossfadeDelay):\n");
        std::printf("  raised cosine (0.5 + 0.5), uncorrelated material: %+.2f dB (theory 10·log10(0.5² + 0.5²) = -3.01)\n",
                    midFadeDb);
        DAWLAB_CHECK_NEAR(midFadeDb, -3.01, 0.15);
        // An equal-power crossfade is only a formula for comparison (this exercise's CrossfadeDelay does not implement it, so it is not a check):
        // at the midpoint gA = gB = cos45°; uncorrelated material 10·log10(2·0.5) = 0 dB; correlated material 20·log10(2·cos45°) = +3.01 dB.
        const double c45 = std::cos(dawlab::kPi / 4.0);
        std::printf("  (Formula comparison) equal-power midpoint gA = gB = cos45°: uncorrelated %+.2f dB, correlated %+.2f dB\n",
                    10.0 * std::log10(2.0 * c45 * c45), dawlab::gainToDb(2.0 * c45));
    }

    DAWLAB_CHECK(!dawlab::hasNanOrInf(crossfade));
    dawlab::writeWav(dawlab::outputPath("l09_ex6_crossfade.wav"), crossfade, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l09_ex6_abrupt.wav"), abrupt, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l09_ex6_ramp.wav"), ramp, kSampleRate);
    std::printf("\nWrote dawlab_output/l09_ex6_crossfade.wav (clean switch), l09_ex6_abrupt.wav (click), "
                "l09_ex6_ramp.wav (brief reverse / speed-change sound)\n\n");

    return dawlab::test::summary("l09_ex6");
}
