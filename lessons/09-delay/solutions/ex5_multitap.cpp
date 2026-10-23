// Exercise 5 solution: multi-tap delay
//
// One delay line, several read points (taps). Each tap has its own delay time, gain and pan.
// The input is first mixed down to mono and written into the delay line, and the output of
// each tap is distributed to left and right according to its pan:
//
//   (L+R)/2 ──►(+)──► delay line ─┬─ tap0 (D0, g0, pan0) ─┐
//               ▲                 ├─ tap1 (D1, g1, pan1) ─┼─(Σ)─► wet L / wet R
//               │                 └─ tap2 (D2, g2, pan2) ─┘
//               └─── ×fb ◄────────── the last tap (before gain / pan)
//
// Unlike "three delays in series", the taps do not affect one another and there is only one
// feedback path (from the last tap back to the input), so it is cheap on the CPU and does not
// accumulate the high-frequency loss of interpolation through cascading.
// Pan uses a constant-power law: gL = cos θ, gR = sin θ, θ = (pan + 1)·π/4.
//
// Self-check: with an impulse going in, every tap must appear at the "correct sample" with
// the "correct amplitude"; with feedback added, the whole set of taps reappears after D_last
// (multiplied by fb).
//
// Run: l09_ex5_solution. Writes dawlab_output/l09_ex5_multitap_dry.wav, l09_ex5_multitap_wet.wav.

#include <dawlab/Common.h>
#include <dawlab/dsp/Delay.h> // only Delay::noteToMs is used, for tempo sync
#include <dawlab/dsp/FractionalDelayLine.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using dawlab::dsp::Delay;
using dawlab::dsp::FractionalDelayLine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class MultiTapDelay {
public:
    static constexpr int kMaxTaps = 8;

    struct Tap {
        float delayMs = 0.0f;
        float gain = 0.0f;
        float pan = 0.0f; // -1 = left, 0 = centre, +1 = right
    };

    void setMaxDelayMs(float ms) noexcept { maxDelayMs_ = std::max(1.0f, ms); }

    void prepare(double sampleRate, int /*maxBlockSize*/, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = std::clamp(numChannels, 1, 2);
        maxDelaySamples_ = dawlab::msToSamples(maxDelayMs_, sampleRate) + 2;
        line_.prepare(maxDelaySamples_, 1); // mono delay line
        prepared_ = true;
        for (int i = 0; i < kMaxTaps; ++i)
            updateTap(i);
        reset();
    }

    void reset() noexcept { line_.reset(); }

    void setNumTaps(int n) noexcept { numTaps_ = std::clamp(n, 0, kMaxTaps); }
    int getNumTaps() const noexcept { return numTaps_; }

    void setTap(int index, float delayMs, float gain, float pan) noexcept {
        if (index < 0 || index >= kMaxTaps)
            return;
        taps_[index].delayMs = std::clamp(delayMs, 0.0f, maxDelayMs_);
        taps_[index].gain = gain;
        taps_[index].pan = std::clamp(pan, -1.0f, 1.0f);
        updateTap(index);
    }
    const Tap& getTap(int index) const noexcept { return taps_[index]; }

    void setFeedback(float fb) noexcept { feedback_ = std::clamp(fb, 0.0f, 0.99f); }
    void setMix(float mix) noexcept { mix_ = std::clamp(mix, 0.0f, 1.0f); }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        if (nch <= 0)
            return;
        float* left = block.getChannelPointer(0);
        float* right = nch > 1 ? block.getChannelPointer(1) : nullptr;
        for (int i = 0; i < n; ++i) {
            const float xl = left[i];
            const float xr = right ? right[i] : xl;
            const float mono = right ? 0.5f * (xl + xr) : xl;

            float wetL = 0.0f, wetR = 0.0f, last = 0.0f;
            for (int t = 0; t < numTaps_; ++t) {
                const TapState& s = state_[t];
                const float v = line_.readCubic(0, s.delaySamples - 1.0f); // read first, push afterwards -> −1
                wetL += s.gainL * v;
                wetR += s.gainR * v;
                last = v; // feedback takes the "raw" delayed signal of the last tap
            }
            line_.push(0, mono + feedback_ * last);

            if (right) {
                left[i] = (1.0f - mix_) * xl + mix_ * wetL;
                right[i] = (1.0f - mix_) * xr + mix_ * wetR;
            } else {
                left[i] = (1.0f - mix_) * xl + mix_ * (wetL + wetR); // mono: left and right are summed (pan loses its meaning)
            }
        }
    }

    // Constant-power pan law (the ConstantPower mode of the chapter 01 Panner)
    static void panGains(float pan, float& gl, float& gr) noexcept {
        const float theta = (pan + 1.0f) * static_cast<float>(dawlab::kPi) * 0.25f;
        gl = std::cos(theta);
        gr = std::sin(theta);
    }

private:
    struct TapState {
        float delaySamples = 2.0f;
        float gainL = 0.0f, gainR = 0.0f;
    };

    void updateTap(int i) noexcept {
        if (!prepared_)
            return;
        const Tap& t = taps_[i];
        TapState& s = state_[i];
        s.delaySamples = std::clamp(static_cast<float>(t.delayMs * 0.001 * sampleRate_), 2.0f,
                                    static_cast<float>(maxDelaySamples_));
        float gl, gr;
        panGains(t.pan, gl, gr);
        s.gainL = t.gain * gl;
        s.gainR = t.gain * gr;
    }

    FractionalDelayLine line_;
    Tap taps_[kMaxTaps];
    TapState state_[kMaxTaps];
    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int numTaps_ = 0;
    int maxDelaySamples_ = 0;
    float maxDelayMs_ = 2000.0f;
    float feedback_ = 0.0f;
    float mix_ = 0.4f;
    bool prepared_ = false;
};

void processInBlocks(MultiTapDelay& d, dawlab::AudioBuffer& b) {
    dawlab::AudioBlock whole(b);
    for (int start = 0; start < b.getNumSamples(); start += kBlockSize)
        d.process(whole.getSubBlock(start, std::min(kBlockSize, b.getNumSamples() - start)));
}

// Three taps at 120 BPM: dotted eighth (left), quarter (right), dotted quarter (centre)
void setupRhythmicTaps(MultiTapDelay& d, double bpm) {
    d.setNumTaps(3);
    d.setTap(0, Delay::noteToMs(bpm, 0.75), 0.6f, -0.6f);
    d.setTap(1, Delay::noteToMs(bpm, 1.0), 0.45f, 0.6f);
    d.setTap(2, Delay::noteToMs(bpm, 1.5), 0.3f, 0.0f);
}

} // namespace

int main() {
    std::printf("=== Chapter 09, Exercise 5: multi-tap delay ===\n\n");
    const double bpm = 120.0;

    // ---- 1. Impulse: position and amplitude of every tap ------------------------------
    {
        MultiTapDelay mt;
        mt.setMaxDelayMs(2000.0f);
        mt.prepare(kSampleRate, kBlockSize, 2);
        setupRhythmicTaps(mt, bpm);
        mt.setFeedback(0.0f);
        mt.setMix(1.0f);
        mt.reset();

        dawlab::AudioBuffer ir = dawlab::makeImpulse(dawlab::secondsToSamples(2.0, kSampleRate), 2); // L = R = 1
        processInBlocks(mt, ir);
        std::printf("Impulse (L = R = 1) through three taps (fb = 0):\n");
        std::printf("   tap   delay(ms)  sample   gain   pan    L expected / measured   R expected / measured\n");
        for (int t = 0; t < mt.getNumTaps(); ++t) {
            const auto& tap = mt.getTap(t);
            const int n = dawlab::msToSamples(tap.delayMs, kSampleRate);
            float gl, gr;
            MultiTapDelay::panGains(tap.pan, gl, gr);
            const float expL = tap.gain * gl, expR = tap.gain * gr;
            const float gotL = ir.getSample(0, n), gotR = ir.getSample(1, n);
            std::printf("   %d   %8.1f  %6d   %.2f  %+.1f   %.4f / %.4f    %.4f / %.4f\n", t, tap.delayMs, n,
                        tap.gain, tap.pan, expL, gotL, expR, gotR);
            DAWLAB_CHECK_NEAR(gotL, expL, 1.0e-4);
            DAWLAB_CHECK_NEAR(gotR, expR, 1.0e-4);
            DAWLAB_CHECK_NEAR(ir.getSample(0, n - 1), 0.0, 1.0e-6); // the previous sample must be 0 (exact position)
            DAWLAB_CHECK_NEAR(ir.getSample(0, n + 1), 0.0, 1.0e-6);
        }
        // Total energy of the whole impulse response = Σ gain² (constant-power pan has gL² + gR² = 1)
        double energy = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < ir.getNumSamples(); ++i)
                energy += static_cast<double>(ir.getSample(ch, i)) * ir.getSample(ch, i);
        DAWLAB_CHECK_NEAR(energy, 0.6 * 0.6 + 0.45 * 0.45 + 0.3 * 0.3, 1.0e-4);
        std::printf("  Total energy %.4f = 0.6² + 0.45² + 0.3² (constant-power pan does not change the energy)\n\n", energy);
    }

    // ---- 2. Feedback: the whole set of taps repeats after the last tap ------------------
    {
        MultiTapDelay mt;
        mt.setMaxDelayMs(2000.0f);
        mt.prepare(kSampleRate, kBlockSize, 2);
        setupRhythmicTaps(mt, bpm);
        mt.setFeedback(0.5f);
        mt.setMix(1.0f);
        mt.reset();
        dawlab::AudioBuffer ir = dawlab::makeImpulse(dawlab::secondsToSamples(3.0, kSampleRate), 2);
        processInBlocks(mt, ir);
        const int dLast = dawlab::msToSamples(mt.getTap(2).delayMs, kSampleRate); // 750 ms = 36000
        const int d0 = dawlab::msToSamples(mt.getTap(0).delayMs, kSampleRate);    // 375 ms = 18000
        float gl, gr;
        MultiTapDelay::panGains(mt.getTap(0).pan, gl, gr);
        const float expL = 0.5f * mt.getTap(0).gain * gl; // fb × the gain of tap0
        std::printf("feedback 0.5: the second appearance of tap0 should be at %d + %d = %d, amplitude %.4f, measured %.4f\n", dLast, d0,
                    dLast + d0, expL, ir.getSample(0, dLast + d0));
        DAWLAB_CHECK_NEAR(ir.getSample(0, dLast + d0), expL, 1.0e-4);
        DAWLAB_CHECK_NEAR(ir.getSample(0, 2 * dLast), 0.5f * mt.getTap(2).gain * 0.70710678f, 1.0e-4);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(ir));
        std::printf("\n");
    }

    // ---- 3. Musical material ---------------------------------------------------------
    {
        dawlab::AudioBuffer seq = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f);
        dawlab::AudioBuffer dry(2, dawlab::secondsToSamples(8.0, kSampleRate));
        for (int ch = 0; ch < 2; ++ch)
            dry.copyFrom(ch, 0, seq, ch, 0, seq.getNumSamples());
        dawlab::AudioBuffer wet = dry;
        MultiTapDelay mt;
        mt.setMaxDelayMs(2000.0f);
        mt.prepare(kSampleRate, kBlockSize, 2);
        setupRhythmicTaps(mt, bpm);
        mt.setFeedback(0.3f);
        mt.setMix(0.4f);
        mt.reset();
        processInBlocks(mt, wet);
        dawlab::writeWav(dawlab::outputPath("l09_ex5_multitap_dry.wav"), dry, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l09_ex5_multitap_wet.wav"), wet, kSampleRate);
        std::printf("Plucked arpeggio: peak dry %.3f / multi-tap %.3f\n", dawlab::peakAbs(dry), dawlab::peakAbs(wet));
        DAWLAB_CHECK(!dawlab::hasNanOrInf(wet));
        DAWLAB_CHECK(dawlab::peakAbs(wet) < 1.0f);
        std::printf("Wrote dawlab_output/l09_ex5_multitap_dry.wav, l09_ex5_multitap_wet.wav\n\n");
    }

    // ---- 4. The numbers behind the problem: cascading three delays accumulates the interpolation's high-frequency loss ----
    {
        // Each is a cubic at Δ = 0.5 (the worst case). One stage is −1.07 dB at 12 kHz; three in series is three times that.
        FractionalDelayLine a, b, c;
        a.prepare(64, 1);
        b.prepare(64, 1);
        c.prepare(64, 1);
        auto once = [&](FractionalDelayLine& line, float x) {
            line.push(0, x);
            return line.readCubic(0, 10.5f);
        };
        const double one = dawlab::measureGainDb([&](float x) { return once(a, x); }, 12000.0, kSampleRate);
        a.reset();
        const double three = dawlab::measureGainDb([&](float x) { return once(c, once(b, once(a, x))); }, 12000.0,
                                                   kSampleRate);
        std::printf("Cubic (Δ = 0.5) at 12 kHz: one stage %+.2f dB, three in series %+.2f dB (multi-tap interpolates each echo only once)\n",
                    one, three);
        DAWLAB_CHECK_NEAR(one, -1.07, 0.02);
        DAWLAB_CHECK_NEAR(three, 3.0 * one, 0.02);
    }

    return dawlab::test::summary("l09_ex5");
}
