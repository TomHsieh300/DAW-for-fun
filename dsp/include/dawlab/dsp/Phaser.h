#pragma once
// Phaser: cascade N first-order allpass filters so that different frequencies get different phase
// shifts, then add the result to the dry signal -> frequencies that end up 180° out of phase cancel
// into notches. An LFO sweeps the allpass centre frequency on an exponential scale. Chapter 10.
//
// Signal flow:
//
//   x ──┬──────────────────────────────────────────────(1-mix)──┐
//       │                                                         ├─► y
//       └─►(+)─► AP1 ─► AP2 ─► … ─► APN ──┬────────────────(mix)─┘
//           ▲                              │
//           └──────── feedback ◄───────────┘
//
// First-order allpass: H(z) = (a + z⁻¹) / (1 + a z⁻¹),  a = (tan(π fc/fs) − 1) / (tan(π fc/fs) + 1)
//   |H| = 1 (the amplitude is unchanged at every frequency), phase φ(ω) = −2·arctan( tan(ω/2) / tan(ωc/2) )
//   At f = fc, φ = −90°; at DC it is 0°; at Nyquist it is −180°.
// The total phase of N cascaded stages is N·φ(ω); added to the dry signal, |1 + e^{jNφ}| = 2|cos(Nφ/2)|,
//   which is zero when Nφ = −(2k+1)π -> the number of notches is N/2 (N even).
//
// Here the Chapter 02 Biquad serves as the container for each stage: setting b2 = a2 = 0 gives a
// first-order section (b0 = a, b1 = 1, b2 = 0, a1 = a, a2 = 0).
//
// Dependencies: Chapter 02 Biquad, and this chapter's Lfo.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

class Phaser {
public:
    static constexpr int kMinStages = 2;
    static constexpr int kMaxStages = 12;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = std::max(1, numChannels);
        for (auto& s : stages_)
            s.prepare(sampleRate, maxBlockSize, numChannels_);
        lastOut_.assign(static_cast<size_t>(numChannels_), 0.0f);
        lfo_.prepare(sampleRate);
        depthSm_.prepare(sampleRate, 20.0);
        centreSm_.prepare(sampleRate, 20.0);
        feedbackSm_.prepare(sampleRate, 20.0);
        mixSm_.prepare(sampleRate, 20.0);
        reset();
    }

    void reset() noexcept {
        for (auto& s : stages_)
            s.reset();
        std::fill(lastOut_.begin(), lastOut_.end(), 0.0f);
        lfo_.reset();
        depthSm_.setCurrentAndTarget(depthSm_.getTarget());
        centreSm_.setCurrentAndTarget(centreSm_.getTarget());
        feedbackSm_.setCurrentAndTarget(feedbackSm_.getTarget());
        mixSm_.setCurrentAndTarget(mixSm_.getTarget());
    }

    // ---- Parameters ----------------------------------------------------------
    void setRateHz(float hz) noexcept { lfo_.setRateHz(std::clamp(hz, 0.0f, 20.0f)); }
    // 0 = no sweep (stays at centre); 1 = sweep ±sweepOctaves octaves
    void setDepth(float depth) noexcept { depthSm_.setTarget(std::clamp(depth, 0.0f, 1.0f)); }
    void setCentreHz(float hz) noexcept { centreSm_.setTarget(std::clamp(hz, 20.0f, 20000.0f)); }
    void setFeedback(float fb) noexcept { feedbackSm_.setTarget(std::clamp(fb, -0.95f, 0.95f)); }
    // Even numbers from 2 to 12; an odd value is rounded down to even
    void setStages(int stages) noexcept {
        stages = std::clamp(stages, kMinStages, kMaxStages);
        stages -= stages % 2;
        // A newly enabled stage may hold state from long ago; clear it first so stale signal does not appear on switching
        for (int s = numStages_; s < stages; ++s)
            stages_[static_cast<size_t>(s)].reset();
        numStages_ = stages;
    }
    void setMix(float mix) noexcept { mixSm_.setTarget(std::clamp(mix, 0.0f, 1.0f)); }
    void setLfoShape(LfoShape shape) noexcept { lfo_.setShape(shape); }
    void setLfoPhase(float phase01) noexcept { lfo_.setPhase(phase01); }
    // At depth = 1 the sweep is ±sweepOctaves (default ±2 octaves = 4 octaves in total)
    void setSweepOctaves(float octaves) noexcept { sweepOctaves_ = std::clamp(octaves, 0.0f, 6.0f); }

    float getRateHz() const noexcept { return lfo_.getRateHz(); }
    float getDepth() const noexcept { return depthSm_.getTarget(); }
    float getCentreHz() const noexcept { return centreSm_.getTarget(); }
    float getFeedback() const noexcept { return feedbackSm_.getTarget(); }
    int getStages() const noexcept { return numStages_; }
    float getMix() const noexcept { return mixSm_.getTarget(); }
    // The allpass centre frequency (Hz) of the most recently processed sample, for the UI / measurement
    // (thread-safe: relaxed atomic)
    float getCurrentCentreHz() const noexcept { return uiCentreHz_.load(); }

    // ---- Formulas (static, for verification in the examples / exercises) -------
    // First-order allpass coefficient: a = (tan(π fc/fs) − 1) / (tan(π fc/fs) + 1)
    static double allpassCoefficient(double fcHz, double sampleRate) noexcept {
        const double t = std::tan(kPi * std::clamp(fcHz, 1.0, 0.49 * sampleRate) / sampleRate);
        return (t - 1.0) / (t + 1.0);
    }
    // Phase of a first-order allpass at frequency f (radians, 0 .. −π): φ = −2 arctan( tan(πf/fs) / tan(πfc/fs) )
    static double allpassPhase(double fHz, double fcHz, double sampleRate) noexcept {
        return -2.0 * std::atan(std::tan(kPi * fHz / sampleRate) / std::tan(kPi * fcHz / sampleRate));
    }
    // With N stages all centred at fc, the frequency of the k-th notch (k = 0 .. N/2−1):
    //   N·φ(f) = −(2k+1)π  ⇒  tan(πf/fs) = tan(πfc/fs) · tan((2k+1)π / (2N))
    static double notchFrequencyHz(int k, int stages, double fcHz, double sampleRate) noexcept {
        const double t = std::tan(kPi * fcHz / sampleRate) * std::tan((2.0 * k + 1.0) * kPi / (2.0 * stages));
        return std::atan(t) * sampleRate / kPi;
    }
    // Wrap fc into the Biquad coefficients of a first-order allpass
    static BiquadCoefficients makeFirstOrderAllpass(double fcHz, double sampleRate) noexcept {
        const double a = allpassCoefficient(fcHz, sampleRate);
        BiquadCoefficients c;
        c.b0 = a;
        c.b1 = 1.0;
        c.b2 = 0.0;
        c.a1 = a;
        c.a2 = 0.0;
        return c;
    }

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
        const float depth = depthSm_.next();
        const float centre = centreSm_.next();
        feedback_ = feedbackSm_.next();
        mix_ = mixSm_.next();

        // Exponential scale: fc = centre · 2^(depth · octaves · lfo), because the ear hears pitch logarithmically
        const float m = lfo_.getNextValue();
        const float octaves = depth * sweepOctaves_ * m;
        currentHz_ = std::clamp(centre * std::exp2(octaves), 20.0f, static_cast<float>(0.45 * sampleRate_));
        uiCentreHz_.store(currentHz_);

        const BiquadCoefficients c = makeFirstOrderAllpass(currentHz_, sampleRate_);
        for (int s = 0; s < numStages_; ++s)
            stages_[static_cast<size_t>(s)].setCoefficients(c);
    }

    float renderSample(int ch, float x) noexcept {
        float w = x + feedback_ * lastOut_[static_cast<size_t>(ch)];
        for (int s = 0; s < numStages_; ++s)
            w = stages_[static_cast<size_t>(s)].processSample(ch, w);
        if (std::abs(w) < 1e-20f)
            w = 0.0f;
        lastOut_[static_cast<size_t>(ch)] = w;
        return (1.0f - mix_) * x + mix_ * w;
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int numStages_ = 4;
    float sweepOctaves_ = 2.0f;

    std::array<Biquad, kMaxStages> stages_;
    std::vector<float> lastOut_;
    Lfo lfo_;

    // Defaults: depth 1, centre 800 Hz, feedback 0.3, mix 0.5
    detail::ModParamSmoother depthSm_{1.0f}, centreSm_{800.0f}, feedbackSm_{0.3f}, mixSm_{0.5f};
    float feedback_ = 0.3f, mix_ = 0.5f;
    float currentHz_ = 800.0f;                   // used internally by the audio thread
    detail::RelaxedAtomicFloat uiCentreHz_{800.0f}; // read by the UI
};

} // namespace dawlab::dsp
