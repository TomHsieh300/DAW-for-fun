#pragma once
// SmoothedValue: turns an abrupt parameter jump into a linear ramp.
//
// Why do we need it? If any parameter that is multiplied into the audio (gain, pan, mix, ...)
// changes instantly between two samples, the signal is effectively multiplied by a step function.
// The spectrum of a step is broadband energy falling off as 1/f, which sounds like a click;
// when a fader is dragged continuously, every small step produces one click, which is what
// we call zipper noise.
// The fix: let the parameter "walk" from its current value to the target over rampSeconds,
// advancing by one small step per sample.
//
// Usage (no allocation at all on the audio thread):
//
//   dawlab::dsp::SmoothedValue gain;
//   gain.reset(48000.0, 0.05);           // prepare: set the ramp length = 50 ms
//   gain.setCurrentAndTargetValue(1.0f); // initial value (do not ramp up from 0)
//   ...
//   gain.setTargetValue(0.5f);           // the UI changed the parameter: over the next 2400 samples it walks linearly to 0.5
//   for (int i = 0; i < n; ++i)
//       y[i] = x[i] * gain.getNextValue();
//
// Design notes:
// - Every setTargetValue() restarts from the *current* value, so there is no jump while a fader is moving.
// - When the target equals the current value no ramp is started (isSmoothing() == false),
//   so process() can take the fast path.
// - No accumulation (current += step): every step is computed directly as
//   current = target - step*countdown, so long ramps and large values (for example delay times
//   in samples, in the tens of thousands) cannot overshoot the target through float accumulation
//   error and then jump back; when the ramp finishes countdown = 0 and current equals target exactly.
// - Not thread-safe: every method must be called from the same thread (the audio thread), and
//   setters are called between blocks. The members are plain float / int, so reading and writing
//   them across threads is a data race (UB); see chapters 13 and 14.
// - This is "linear" smoothing: it finishes in a fixed time with a constant slope. One-pole
//   exponential smoothing (never quite arrives, but behaves more like an analog circuit) is
//   implemented in exercise 3 of this chapter.

#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

class SmoothedValue {
public:
    SmoothedValue() = default;
    explicit SmoothedValue(float initialValue) noexcept : current_(initialValue), target_(initialValue) {}

    // Sets the sample rate and the ramp time. Stops any ramp in progress (current = target).
    // Call it during prepare(); rampSeconds = 0 means no smoothing (jump straight to the target).
    void reset(double sampleRate, double rampSeconds) noexcept {
        rampSamples_ = std::max(0, static_cast<int>(std::lround(rampSeconds * sampleRate)));
        current_ = target_;
        countdown_ = 0;
        step_ = 0.0f;
    }

    // Changes only the ramp length without interrupting the current value; if a ramp is in
    // progress, it restarts from the current value with the new length.
    void setRampLength(int numSamples) noexcept {
        rampSamples_ = std::max(0, numSamples);
        if (countdown_ > 0)
            startRamp();
    }

    // Sets the current value and the target at the same time (for example for initialization,
    // or when a reset() should not produce a ramp)
    void setCurrentAndTargetValue(float value) noexcept {
        current_ = target_ = value;
        countdown_ = 0;
        step_ = 0.0f;
    }

    // Sets a new target: starting from the *current* value, walks linearly to the target over rampSamples_ samples
    void setTargetValue(float value) noexcept {
        if (value == target_)
            return;
        target_ = value;
        startRamp();
    }

    // Returns the value to use for the next sample (call once per sample)
    float getNextValue() noexcept {
        if (countdown_ <= 0)
            return target_;
        --countdown_;
        // Linear: current[n] = target - step*countdown[n] (equal to start + n*step, but without
        // accumulating error); when countdown reaches 0 the value is exactly target
        current_ = valueAtCountdown();
        return current_;
    }

    float getCurrentValue() const noexcept { return countdown_ > 0 ? current_ : target_; }
    float getTargetValue() const noexcept { return target_; }
    bool isSmoothing() const noexcept { return countdown_ > 0; }
    int getRampSamples() const noexcept { return rampSamples_; }
    int getRemainingSamples() const noexcept { return countdown_; }

    // Skips numSamples samples (for example when this stretch of the buffer is actually silent
    // and does not need a per-sample multiplication)
    void skip(int numSamples) noexcept {
        if (countdown_ <= 0 || numSamples <= 0)
            return;
        if (numSamples >= countdown_) {
            countdown_ = 0;
            current_ = target_;
            return;
        }
        countdown_ -= numSamples;
        current_ = valueAtCountdown();
    }

    // Convenience: multiplies n consecutive smoothed values into data[0..n)
    void applyGain(float* data, int n) noexcept {
        if (!isSmoothing()) {
            const float g = target_;
            for (int i = 0; i < n; ++i)
                data[i] *= g;
            return;
        }
        for (int i = 0; i < n; ++i)
            data[i] *= getNextValue();
    }

    // Convenience: the whole block, with all channels sharing one ramp (every channel of the same frame is multiplied by the same value)
    void applyGain(AudioBlock block) noexcept {
        const int numChannels = block.getNumChannels();
        const int numSamples = block.getNumSamples();
        if (!isSmoothing()) {
            block.multiplyBy(target_);
            return;
        }
        for (int i = 0; i < numSamples; ++i) {
            const float g = getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                block.getChannelPointer(ch)[i] *= g;
        }
    }

private:
    // The value on the ramp with countdown_ steps remaining; the multiplication is done in double so that large values keep the full precision of a float
    float valueAtCountdown() const noexcept {
        if (countdown_ <= 0)
            return target_;
        return static_cast<float>(static_cast<double>(target_)
                                  - static_cast<double>(step_) * static_cast<double>(countdown_));
    }

    void startRamp() noexcept {
        if (rampSamples_ <= 0) {
            current_ = target_;
            countdown_ = 0;
            step_ = 0.0f;
            return;
        }
        countdown_ = rampSamples_;
        step_ = (target_ - current_) / static_cast<float>(rampSamples_);
    }

    float current_ = 0.0f;
    float target_ = 0.0f;
    float step_ = 0.0f;
    int rampSamples_ = 0;
    int countdown_ = 0;
};

} // namespace dawlab::dsp
