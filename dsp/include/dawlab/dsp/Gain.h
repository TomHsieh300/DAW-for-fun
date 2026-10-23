#pragma once
// Gain: the most basic DSP building block -- multiply the signal by a factor.
//
//   y[n] = g * x[n],  g = 10^(dB/20)  (g is negated when polarity invert is on)
//
// It looks like a single multiplication, but getting it right involves three things:
// 1. The parameter is set in dB (the ear perceives loudness logarithmically); internally it is
//    converted to a linear factor before being multiplied into the signal.
// 2. When the factor changes it is smoothed linearly by a SmoothedValue (50 ms by default) to
//    avoid clicks / zipper noise.
// 3. Polarity invert goes through the same ramp (+g -> -g passes through 0), so toggling it does not pop.
//
// Usage (setters and process() are called from the same thread, between blocks; see the
// note under "Parameters" below):
//   dawlab::dsp::Gain gain;
//   gain.prepare(48000.0, 512, 2);
//   gain.setGainDb(-6.0f);
//   gain.process(block);           // in-place; the whole block, all channels share one ramp
//
// Convention for processSample(): call it per "frame". Channel 0 advances the ramp and the
// other channels reuse the same value. In other words, for every frame call processSample(0, x0) first,
// then processSample(1, x1).

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

class Gain {
public:
    // The only place allowed to allocate (this class does not actually need to allocate, but the interface is kept uniform)
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        (void)numChannels;
        sampleRate_ = sampleRate;
        smoother_.reset(sampleRate, rampSeconds_);
        smoother_.setCurrentAndTargetValue(targetLinear());
        lastValue_ = smoother_.getCurrentValue();
    }

    // Clears the internal state: snaps the ramp to the target value (there is no filter memory to clear)
    void reset() noexcept {
        smoother_.setCurrentAndTargetValue(targetLinear());
        lastValue_ = smoother_.getCurrentValue();
    }

    // ---- Parameters ----
    // Usage rule: parameter setters must be called between blocks, from the same thread that calls process().
    // The components in this chapter are not thread-safe: gainDb_ and the SmoothedValue state are
    // plain float / int, so the UI thread writing while the audio thread reads is a data race
    // (undefined behavior) under the C++ memory model; "an aligned float write is atomic on the
    // hardware" is not a basis for thread safety.
    // For passing parameters across threads (std::atomic, a parameter system) see chapters 13 and 14.

    // Maximum allowed gain (+40 dB = 100x). Anything larger is almost certainly a bug (for example passing a linear factor as dB).
    static constexpr float kMaxGainDb = 40.0f;

    // db <= kMinusInfinityDb (including -inf) = silence; NaN is ignored (the previous value is kept); values above kMaxGainDb are clamped.
    void setGainDb(float db) noexcept {
        if (std::isnan(db))
            return; // NaN must never flow into the audio: keep the existing setting
        gainDb_ = std::min(db, kMaxGainDb);
        smoother_.setTargetValue(targetLinear());
    }

    // Sets the gain as a linear factor (negative or 0 is treated as -inf dB)
    void setGainLinear(float linear) noexcept { setGainDb(gainToDb(linear)); }

    float getGainDb() const noexcept { return gainDb_; }
    float getGainLinear() const noexcept { return dbToGain(gainDb_); }

    // Polarity invert (the polarity button): multiplies by -1. It uses the same ramp, so toggling
    // walks smoothly from +g through 0 to -g.
    void setPolarityInverted(bool inverted) noexcept {
        inverted_ = inverted;
        smoother_.setTargetValue(targetLinear());
    }
    bool isPolarityInverted() const noexcept { return inverted_; }

    // Ramp length (default 0.05 s = 50 ms). It may also be called after prepare and does not interrupt the current value.
    void setRampSeconds(double seconds) noexcept {
        rampSeconds_ = std::max(0.0, seconds);
        if (sampleRate_ > 0.0)
            smoother_.setRampLength(static_cast<int>(std::lround(rampSeconds_ * sampleRate_)));
    }
    double getRampSeconds() const noexcept { return rampSeconds_; }

    // The factor actually being multiplied into the signal right now (while a ramp is running it lies between the old and new values); for UI or tests
    float getCurrentGainLinear() const noexcept { return smoother_.getCurrentValue(); }
    bool isSmoothing() const noexcept { return smoother_.isSmoothing(); }

    // ---- Processing ----

    // Per sample: channel 0 advances the ramp and the other channels reuse the same value (see the header comment)
    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            lastValue_ = smoother_.getNextValue();
        return x * lastValue_;
    }

    // Processes the whole block in place: every channel of the same frame is multiplied by the same gain value
    void process(AudioBlock block) noexcept {
        smoother_.applyGain(block);
        lastValue_ = smoother_.getCurrentValue();
    }

private:
    float targetLinear() const noexcept {
        const float g = dbToGain(gainDb_);
        return inverted_ ? -g : g;
    }

    double sampleRate_ = 0.0;
    double rampSeconds_ = 0.05;
    float gainDb_ = 0.0f;
    bool inverted_ = false;
    float lastValue_ = 1.0f;
    SmoothedValue smoother_{1.0f};
};

} // namespace dawlab::dsp
