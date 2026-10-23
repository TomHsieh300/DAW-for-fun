#pragma once
// Tremolo: amplitude modulation. The gain swings with the LFO between 1 and (1 − depth). Chapter 10.
//
//   y = x · g(t),  g(t) = 1 − depth · (1 − lfo(t)) / 2        (lfo = +1 -> g = 1; lfo = −1 -> g = 1 − depth)
//
// With a sine LFO, g(t) = (1 − depth/2) + (depth/2)·sin(2π r t), so a sine input at frequency f
// becomes f (amplitude ×(1 − depth/2)) plus two sidebands at f ± r (amplitude ×depth/4 each) --
// this is the spectrum of AM.
//
// autopan: the right channel uses −lfo, so when the left is loud the right is quiet and the image
// moves back and forth.
// The two channel gains sum to 2 − depth, a constant (a linear pan law), so the total loudness
// stays roughly the same to the ear.
//
// Dependencies: this chapter's Lfo.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

class Tremolo {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        numChannels_ = std::max(1, numChannels);
        lfo_.prepare(sampleRate);
        depthSm_.prepare(sampleRate, 20.0);
        reset();
    }

    void reset() noexcept {
        lfo_.reset();
        depthSm_.setCurrentAndTarget(depthSm_.getTarget());
        gain_[0] = gain_[1] = 1.0f;
        publishGains();
    }

    // ---- Parameters ----------------------------------------------------------
    void setRateHz(float hz) noexcept { lfo_.setRateHz(std::clamp(hz, 0.0f, 50.0f)); }
    void setDepth(float depth) noexcept { depthSm_.setTarget(std::clamp(depth, 0.0f, 1.0f)); }
    void setShape(LfoShape shape) noexcept { lfo_.setShape(shape); }
    void setAutoPan(bool autoPan) noexcept { autoPan_ = autoPan; }
    void setLfoPhase(float phase01) noexcept { lfo_.setPhase(phase01); }
    // Edge smoothing (ms) for Square / Saw / S&H; 0 = off (clicks; for demonstration)
    void setSmoothingMs(float ms) noexcept { lfo_.setSmoothingMs(ms); }

    float getRateHz() const noexcept { return lfo_.getRateHz(); }
    float getDepth() const noexcept { return depthSm_.getTarget(); }
    LfoShape getShape() const noexcept { return lfo_.getShape(); }
    bool getAutoPan() const noexcept { return autoPan_; }
    // The gain applied to a channel for the most recently processed sample (for the UI / measurement;
    // thread-safe). The audio thread computes with a plain float each sample, then does a relaxed
    // store into the atomic, and the UI thread reads the atomic.
    // Channel 1 differs from channel 0 only when autopan is on.
    float getCurrentGain(int channel) const noexcept { return uiGain_[channel == 1 ? 1 : 0].load(); }

    // ---- Processing ----------------------------------------------------------
    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            advanceModulation();
        return x * gain_[(autoPan_ && channel == 1) ? 1 : 0];
    }

    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advanceModulation();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                d[i] *= gain_[(autoPan_ && ch == 1) ? 1 : 0];
            }
        }
    }

private:
    void advanceModulation() noexcept {
        const float depth = depthSm_.next();
        const float m = lfo_.getNextValue();
        gain_[0] = 1.0f - depth * 0.5f * (1.0f - m); // lfo = +1 -> 1, lfo = −1 -> 1 − depth
        gain_[1] = 1.0f - depth * 0.5f * (1.0f + m); // inverted (the right channel for autopan)
        publishGains();
    }

    void publishGains() noexcept {
        uiGain_[0].store(gain_[0]);
        uiGain_[1].store(autoPan_ ? gain_[1] : gain_[0]); // the value actually applied to the right channel
    }

    int numChannels_ = 2;
    bool autoPan_ = false;
    Lfo lfo_;
    detail::ModParamSmoother depthSm_{0.5f}; // default depth 0.5
    float gain_[2] = {1.0f, 1.0f};                           // used internally by the audio thread
    detail::RelaxedAtomicFloat uiGain_[2] = {detail::RelaxedAtomicFloat{1.0f}, detail::RelaxedAtomicFloat{1.0f}}; // read by the UI
};

} // namespace dawlab::dsp
