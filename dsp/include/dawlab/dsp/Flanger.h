#pragma once
// Flanger: a very short delay (0.5-10 ms) + LFO sweep + feedback, forming a "sweeping comb filter".
// Chapter 10.
//
// Signal flow:
//
//   x ──┬───────────────────────────────────────(1-mix)──┐
//       │                                                  ├─► y
//       └─►(+)─► FractionalDelayLine ─ read(d(t)) ──┬──(mix)─┘
//           ▲                                       │
//           └──────────── feedback ◄────────────────┘
//
// With no feedback and mix = 0.5 this is the Chapter 09 FIR comb:
//   |H(f)| = |0.5 + 0.5 e^{-j2πf d}| = |cos(π f d)|
//   notches at f = (2k+1) / (2d), peaks at f = k / d (d in seconds).
// When the LFO sweeps the delay d(t), the whole row of notches slides up and down together --
// that is the jet-plane "whoosh".
//
// Delay time d(t) = delayMs + depthMs · (1 + lfo) / 2, sweeping from delayMs to delayMs + depthMs.
//
// Dependencies: Chapter 09 FractionalDelayLine, and this chapter's Lfo.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

class Flanger {
public:
    static constexpr float kMinDelayMs = 0.1f;
    static constexpr float kMaxDelayMs = 20.0f;
    static constexpr float kMaxDepthMs = 20.0f;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        sampleRate_ = sampleRate;
        numChannels_ = std::max(1, numChannels);
        const int maxDelay = msToSamples(kMaxDelayMs + kMaxDepthMs, sampleRate) + 8;
        delay_.prepare(maxDelay, numChannels_);
        lfo_.prepare(sampleRate);
        delaySm_.prepare(sampleRate, 20.0);
        depthSm_.prepare(sampleRate, 20.0);
        feedbackSm_.prepare(sampleRate, 20.0);
        mixSm_.prepare(sampleRate, 20.0);
        reset();
    }

    void reset() noexcept {
        delay_.reset();
        lfo_.reset();
        delaySm_.setCurrentAndTarget(delaySm_.getTarget());
        depthSm_.setCurrentAndTarget(depthSm_.getTarget());
        feedbackSm_.setCurrentAndTarget(feedbackSm_.getTarget());
        mixSm_.setCurrentAndTarget(mixSm_.getTarget());
    }

    // ---- Parameters ----------------------------------------------------------
    void setRateHz(float hz) noexcept { lfo_.setRateHz(std::clamp(hz, 0.0f, 20.0f)); }
    void setDepthMs(float ms) noexcept { depthSm_.setTarget(std::clamp(ms, 0.0f, kMaxDepthMs)); }
    // Base delay (the lower end of the sweep range)
    void setDelayMs(float ms) noexcept { delaySm_.setTarget(std::clamp(ms, kMinDelayMs, kMaxDelayMs)); }
    // Positive: the peaks get sharper (a resonant feel at f = k/d); negative: the notches are filled in
    // and the peaks move to (2k+1)/(2d), giving a more "hollow" sound
    void setFeedback(float fb) noexcept { feedbackSm_.setTarget(std::clamp(fb, -0.99f, 0.99f)); }
    void setMix(float mix) noexcept { mixSm_.setTarget(std::clamp(mix, 0.0f, 1.0f)); }
    void setLfoShape(LfoShape shape) noexcept { lfo_.setShape(shape); }
    // Park the LFO at a given phase (use after setting rate to 0); for measuring the frequency response
    void setLfoPhase(float phase01) noexcept { lfo_.setPhase(phase01); }

    float getRateHz() const noexcept { return lfo_.getRateHz(); }
    float getDepthMs() const noexcept { return depthSm_.getTarget(); }
    float getDelayMs() const noexcept { return delaySm_.getTarget(); }
    float getFeedback() const noexcept { return feedbackSm_.getTarget(); }
    float getMix() const noexcept { return mixSm_.getTarget(); }
    // The delay (ms) actually used by the most recently processed sample, for the UI or measurement
    // (thread-safe: relaxed atomic)
    float getCurrentDelayMs() const noexcept { return uiDelayMs_.load(); }

    // Theoretical notch frequency (mix = 0.5, feedback = 0): f_k = (2k+1) / (2 d)
    static double notchFrequencyHz(int k, double delayMs) noexcept {
        return (2.0 * k + 1.0) / (2.0 * delayMs * 0.001);
    }
    // Theoretical peak frequency: f_k = k / d
    static double peakFrequencyHz(int k, double delayMs) noexcept { return k / (delayMs * 0.001); }

    // ---- Processing ----------------------------------------------------------
    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            advanceModulation();
        return renderSample(channel, x);
    }

    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advanceModulation();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                d[i] = renderSample(ch, d[i]);
            }
        }
    }

private:
    void advanceModulation() noexcept {
        const float baseMs = delaySm_.next();
        const float depthMs = depthSm_.next();
        feedback_ = feedbackSm_.next();
        mix_ = mixSm_.next();
        const float m = lfo_.getNextValue();                     // -1..1
        currentDelayMs_ = baseMs + depthMs * 0.5f * (1.0f + m);  // delayMs .. delayMs + depthMs
        uiDelayMs_.store(currentDelayMs_);
        const float maxRead = static_cast<float>(delay_.getMaxDelaySamples() - 3);
        currentDelaySamples_ = std::clamp(static_cast<float>(currentDelayMs_ * sampleRate_ * 0.001), 1.0f, maxRead);
    }

    float renderSample(int ch, float x) noexcept {
        // With feedback we must read first and write second: the value written has to include the
        // delayed sample we just read.
        // The read happens before the push, so read(d - 1) is the value d samples ago
        // (after a push, read(0) = the sample just written; before a push, read(0) = the previous sample).
        const float delayed = delay_.readCubic(ch, std::max(1.0f, currentDelaySamples_ - 1.0f));
        float w = x + feedback_ * delayed;
        if (std::abs(w) < 1e-20f) // flush tiny values in the loop to 0 to avoid denormals
            w = 0.0f;
        delay_.push(ch, w);
        return (1.0f - mix_) * x + mix_ * delayed;
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    FractionalDelayLine delay_;
    Lfo lfo_;

    // Defaults: delay 1 ms, depth 2 ms, feedback 0.5, mix 0.5
    detail::ModParamSmoother delaySm_{1.0f}, depthSm_{2.0f}, feedbackSm_{0.5f}, mixSm_{0.5f};
    float feedback_ = 0.5f, mix_ = 0.5f;
    float currentDelayMs_ = 1.0f;                       // used internally by the audio thread
    detail::RelaxedAtomicFloat uiDelayMs_{1.0f};        // read by the UI
    float currentDelaySamples_ = 48.0f;
};

} // namespace dawlab::dsp
