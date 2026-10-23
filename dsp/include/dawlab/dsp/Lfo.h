#pragma once
// Lfo: Low-Frequency Oscillator. Chapter 10.
//
// An LFO is not a signal you listen to; it is a signal that controls other parameters
// (delay time, gain, filter frequency, ...). Its output is always in -1..+1, and each
// effect decides what range to map that value onto.
//
// Design notes:
// - There is a single phase accumulator (0..1) that advances by rate / sampleRate per sample.
//   Every waveform is a function of the phase, so when the rate changes the phase stays
//   continuous and never jumps.
// - Square / Saw / Sample&Hold have instantaneous jumps. The LFO's own frequency is so low
//   that aliasing is not a concern, but a sudden jump in the gain or delay it controls
//   produces a click in the audio. These shapes therefore pass through a 1 ms one-pole
//   smoother by default (adjustable with setSmoothingMs; 0 = off).
// - Sine / Triangle are already continuous and are not smoothed, so the waveform stays exact
//   (a chorus, for example, needs it to compute the pitch deviation).

#include <dawlab/DspMath.h>
#include <dawlab/Signals.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace dawlab::dsp {

enum class LfoShape { Sine, Triangle, Saw, Square, SampleAndHold };

namespace detail {

// First-order (one-pole) parameter smoother shared by the Chapter 10 effects:
//   y[n] = y[n-1] + k (target - y[n-1]),  k = 1 - exp(-1 / (τ · fs))
// τ is the time constant: after τ the smoother has covered 63% of the distance, and after
// about 5τ it is considered to have arrived.
class ModParamSmoother {
public:
    explicit ModParamSmoother(float initialValue = 0.0f) noexcept : current_(initialValue), target_(initialValue) {}

    void prepare(double sampleRate, double timeConstantMs) noexcept {
        const double tauSamples = std::max(1.0, timeConstantMs * 0.001 * sampleRate);
        coeff_ = static_cast<float>(1.0 - std::exp(-1.0 / tauSamples));
    }
    void setCurrentAndTarget(float v) noexcept { current_ = target_ = v; }
    void setTarget(float v) noexcept { target_ = v; }
    float getTarget() const noexcept { return target_; }
    float getCurrent() const noexcept { return current_; }

    float next() noexcept {
        current_ += coeff_ * (target_ - current_);
        if (std::abs(target_ - current_) < 1e-6f) // Snap to the target once converged, avoiding denormals and a forever-almost-there tail
            current_ = target_;
        return current_;
    }

private:
    float current_ = 0.0f;
    float target_ = 0.0f;
    float coeff_ = 1.0f;
};

// A measured value for the UI thread to read (e.g. the current gain, delay, or center
// frequency): the audio thread writes it and the UI thread reads it, so it must be a
// std::atomic, otherwise it is a data race (authoring guide 4.1). We only need to read some
// recent value and do not need to synchronize with other variables, so memory_order_relaxed
// is enough (on x86 / ARM it is a plain load / store).
// std::atomic itself is not copyable; this wrapper keeps the owning effect class copyable
// (e.g. returnable by value).
class RelaxedAtomicFloat {
public:
    explicit RelaxedAtomicFloat(float v = 0.0f) noexcept : value_(v) {}
    RelaxedAtomicFloat(const RelaxedAtomicFloat& other) noexcept : value_(other.load()) {}
    RelaxedAtomicFloat& operator=(const RelaxedAtomicFloat& other) noexcept {
        store(other.load());
        return *this;
    }
    void store(float v) noexcept { value_.store(v, std::memory_order_relaxed); }
    float load() const noexcept { return value_.load(std::memory_order_relaxed); }

private:
    std::atomic<float> value_;
};

} // namespace detail

class Lfo {
public:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateIncrement();
        updateSmoothing();
        reset();
    }

    // Return to the initial state: the phase goes back to the start phase (the value set by
    // setPhase), and the Sample & Hold random sequence restarts from its seed. So after every
    // reset() the same input always produces the same output (an offline bounce is reproducible).
    void reset() noexcept {
        phase_ = startPhase_;
        noise_ = NoiseGenerator(seed_);
        hold_ = noise_.next();
        smoothed_ = rawValue();
    }

    void setShape(LfoShape shape) noexcept { shape_ = shape; }
    LfoShape getShape() const noexcept { return shape_; }

    // 0 Hz is legal: the LFO stays frozen at its current phase (handy when measuring a frequency response)
    void setRateHz(float hz) noexcept {
        rateHz_ = std::max(0.0f, hz);
        updateIncrement();
    }
    float getRateHz() const noexcept { return rateHz_; }

    // Set the *current* phase (0..1 = one cycle) and remember it as the start phase for reset().
    // The voices of a chorus and the left/right channels of an autopan are offset from each other
    // by using different start phases.
    void setPhase(float phase01) noexcept {
        startPhase_ = wrap(phase01);
        phase_ = startPhase_;
    }
    float getPhase() const noexcept { return static_cast<float>(phase_); }

    // Smoothing time constant (ms) for the edges of Square / Saw / S&H. 0 = no smoothing (clicks; for demonstration)
    void setSmoothingMs(float ms) noexcept {
        smoothingMs_ = std::max(0.0f, ms);
        updateSmoothing();
    }
    float getSmoothingMs() const noexcept { return smoothingMs_; }

    // Random seed for Sample & Hold.
    // The seed is hashed first: when xorshift starts from a very small seed, its first few
    // outputs are poorly distributed (e.g. all on the same side), and S&H would not sound random.
    // The new seed takes effect at the next reset() (or prepare()).
    void setSeed(uint32_t seed) noexcept { seed_ = mixSeed(seed); }

    // Return the next value (-1..1) and advance the phase by one sample
    float getNextValue() noexcept {
        const float raw = rawValue();

        phase_ += increment_;
        if (phase_ >= 1.0) {
            phase_ -= 1.0;
            hold_ = noise_.next(); // S&H: draw a new random value at the start of each cycle
        }

        if (needsSmoothing() && smoothCoeff_ < 1.0f) {
            smoothed_ += smoothCoeff_ * (raw - smoothed_);
            return smoothed_;
        }
        smoothed_ = raw;
        return raw;
    }

    // Stateless waveform function: given a phase (0..1), return -1..1.
    // The chorus uses it to derive, from one phase, values for channels offset by 90°.
    // S&H needs state, so this returns 0 for it.
    static float shapeValue(LfoShape shape, float phase01) noexcept {
        const float p = wrap(phase01);
        switch (shape) {
        case LfoShape::Sine:
            return static_cast<float>(std::sin(kTwoPi * p));
        case LfoShape::Triangle: // 0 -> +1 (p=0.25) -> -1 (p=0.75) -> 0, in phase with the sine
            if (p < 0.25f)
                return 4.0f * p;
            if (p < 0.75f)
                return 2.0f - 4.0f * p;
            return 4.0f * p - 4.0f;
        case LfoShape::Saw: // Ramps from -1 up to +1, then drops back to -1 instantly at the end of the cycle
            return 2.0f * p - 1.0f;
        case LfoShape::Square:
            return p < 0.5f ? 1.0f : -1.0f;
        case LfoShape::SampleAndHold:
        default:
            return 0.0f;
        }
    }

    // Tempo sync: beats = how many beats one LFO cycle lasts (1 = quarter note, 0.5 = eighth note,
    // 0.75 = dotted eighth, 1/3 = one eighth-note triplet; three eighth triplets fill one beat).
    // rate = bpm / 60 / beats
    static float tempoToRateHz(double bpm, double beats) noexcept {
        if (bpm <= 0.0 || beats <= 0.0)
            return 0.0f;
        return static_cast<float>(bpm / 60.0 / beats);
    }

private:
    // 32-bit integer hash (lowbias32): scatters nearby seeds into seemingly unrelated values
    static uint32_t mixSeed(uint32_t s) noexcept {
        s ^= s >> 16;
        s *= 0x7feb352du;
        s ^= s >> 15;
        s *= 0x846ca68bu;
        s ^= s >> 16;
        return s != 0u ? s : 1u;
    }

    static float wrap(float p) noexcept {
        p -= std::floor(p);
        return p < 0.0f ? 0.0f : (p >= 1.0f ? 0.0f : p);
    }

    bool needsSmoothing() const noexcept {
        return shape_ == LfoShape::Square || shape_ == LfoShape::Saw || shape_ == LfoShape::SampleAndHold;
    }

    float rawValue() const noexcept {
        return shape_ == LfoShape::SampleAndHold ? hold_ : shapeValue(shape_, static_cast<float>(phase_));
    }

    void updateIncrement() noexcept { increment_ = rateHz_ / sampleRate_; }

    void updateSmoothing() noexcept {
        if (smoothingMs_ <= 0.0f) {
            smoothCoeff_ = 1.0f;
            return;
        }
        const double tau = smoothingMs_ * 0.001 * sampleRate_;
        smoothCoeff_ = static_cast<float>(1.0 - std::exp(-1.0 / std::max(1.0, tau)));
    }

    double sampleRate_ = 48000.0;
    LfoShape shape_ = LfoShape::Sine;
    float rateHz_ = 1.0f;
    double increment_ = 1.0 / 48000.0; // Phase is a double: at very low rates, float accumulation error would shift the frequency by several %
    double phase_ = 0.0;
    float startPhase_ = 0.0f;
    float hold_ = 0.0f;
    float smoothed_ = 0.0f;
    float smoothingMs_ = 1.0f;
    float smoothCoeff_ = 1.0f;
    uint32_t seed_ = mixSeed(20231u);
    NoiseGenerator noise_{seed_};
};

} // namespace dawlab::dsp
