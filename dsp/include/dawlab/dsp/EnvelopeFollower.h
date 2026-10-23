#pragma once
// Chapter 04: EnvelopeFollower (envelope detector) and AttackReleaseFilter (attack / release smoother).
//
// These two classes are the shared foundation of all dynamics processors (compressor, limiter, gate, expander, de-esser):
//
//   AttackReleaseFilter  A one-pole smoother that uses the attack coefficient when rising and the release coefficient
//                        when falling. Its input can be any quantity (linear amplitude, x², or a dB value), so the
//                        compressor can use it to smooth gain reduction in the log (dB) domain, while
//                        EnvelopeFollower uses it to smooth |x| in the linear domain.
//   EnvelopeFollower     Takes raw samples and returns a linear-amplitude envelope (Peak or RMS mode).
//                        The gate / expander / de-esser of chapter 08 will reuse it directly.
//
// Core math (one-pole smoothing):
//   y[n] = α·y[n-1] + (1-α)·x[n],   α = exp(-1 / (τ·fs))
//   τ is the time constant (in seconds): for a step input, y reaches 63.2% (1 - 1/e) of the target at t = τ,
//   and 90% at t = 2.3τ. See lessons/04-compressor/README.md for the full derivation.
//
// Depends only on common/ (dawlab/DspMath.h) and the standard library, not on any other chapter.

#include <dawlab/DspMath.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

// Detector mode: Peak = look at the instantaneous absolute value |x|; Rms = take the moving average of x², then the square root
enum class DetectorMode { Peak, Rms };

// The two smoother structures (Giannoulis, Massberg & Reiss 2012, eq. 16 and eq. 17):
//   Branching: switch between the attack / release coefficients depending on whether the target is above or below
//              the current state (the most intuitive formulation)
//   Decoupled: first a peak-hold that "rises instantly and decays with the release", then smooth it with the attack
//              coefficient; for periodic signals such as a sine it does not underestimate the peak because of ripple,
//              and the attack curve is exactly 1 - e^(-t/τ)
enum class SmoothingMode { Branching, Decoupled };

// α = exp(-1 / (τ·fs)), with τ given in milliseconds. τ <= 0 means "instant" (α = 0, so the output equals the input).
inline float attackReleaseCoefficient(double timeMs, double sampleRate) noexcept {
    if (timeMs <= 0.0 || sampleRate <= 0.0)
        return 0.0f;
    return static_cast<float>(std::exp(-1.0 / (timeMs * 0.001 * sampleRate)));
}

// Inverse conversion: given α and fs, find τ (in milliseconds). τ = -1 / (fs · ln α)
inline double coefficientToTimeMs(double alpha, double sampleRate) noexcept {
    if (alpha <= 0.0 || alpha >= 1.0 || sampleRate <= 0.0)
        return 0.0;
    return -1000.0 / (sampleRate * std::log(alpha));
}

class AttackReleaseFilter {
public:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        updateCoefficients();
        reset();
    }

    void reset() noexcept {
        y_ = 0.0f;
        peakHold_ = 0.0f;
    }

    void setAttackMs(float ms) noexcept {
        attackMs_ = std::max(0.0f, ms);
        updateCoefficients();
    }

    void setReleaseMs(float ms) noexcept {
        releaseMs_ = std::max(0.0f, ms);
        updateCoefficients();
    }

    void setMode(SmoothingMode mode) noexcept { mode_ = mode; }

    float getAttackMs() const noexcept { return attackMs_; }
    float getReleaseMs() const noexcept { return releaseMs_; }
    float getAttackCoefficient() const noexcept { return attackCoeff_; }
    float getReleaseCoefficient() const noexcept { return releaseCoeff_; }
    SmoothingMode getMode() const noexcept { return mode_; }
    float getCurrentValue() const noexcept { return y_; }

    // Set the state directly to some value (e.g. when a compressor wants to start from "no compression")
    void setCurrentValue(float value) noexcept {
        y_ = value;
        peakHold_ = value;
    }

    // Feed in a "target value" and return the smoothed value.
    float process(float target) noexcept {
        // Non-finite guard: as soon as a recursive filter swallows one Inf / NaN, its state is stuck at NaN forever
        // (any operation involving NaN yields NaN), never recovers, and can only be reset().
        // So a bad input is simply treated as 0 (this sample is ignored) and the state stays finite.
        if (!std::isfinite(target))
            target = 0.0f;
        if (mode_ == SmoothingMode::Branching) {
            // y[n] = α·y[n-1] + (1-α)·x[n]; use the attack coefficient when x is above y, otherwise the release coefficient
            const float a = target > y_ ? attackCoeff_ : releaseCoeff_;
            y_ = target + a * (y_ - target); // Another way of writing the same formula, with one fewer multiplication
        } else {
            // Decoupled: y1[n] = max(x[n], αR·y1[n-1] + (1-αR)·x[n])   <- peak-hold: rises instantly, decays with the release
            //            y[n]  = αA·y[n-1] + (1-αA)·y1[n]               <- attack smoothing
            peakHold_ = std::max(target, target + releaseCoeff_ * (peakHold_ - target));
            y_ = peakHold_ + attackCoeff_ * (y_ - peakHold_);
        }
        // Denormal guard: a recursive decay down to a tiny value becomes denormal and slows the CPU down; flush it to 0.
        // Also block non-finite once more (e.g. a very large finite input overflowing to Inf during the computation),
        // so that the state can always recover.
        if (std::abs(y_) < kDenormalFlush || !std::isfinite(y_))
            y_ = 0.0f;
        if (std::abs(peakHold_) < kDenormalFlush || !std::isfinite(peakHold_))
            peakHold_ = 0.0f;
        return y_;
    }

private:
    static constexpr float kDenormalFlush = 1.0e-15f;

    void updateCoefficients() noexcept {
        attackCoeff_ = attackReleaseCoefficient(attackMs_, sampleRate_);
        releaseCoeff_ = attackReleaseCoefficient(releaseMs_, sampleRate_);
    }

    double sampleRate_ = 48000.0;
    float attackMs_ = 10.0f;
    float releaseMs_ = 100.0f;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;
    SmoothingMode mode_ = SmoothingMode::Branching;
    float y_ = 0.0f;
    float peakHold_ = 0.0f;
};

// Envelope follower: takes raw samples and outputs a linear-amplitude envelope (always >= 0).
//
//   Peak mode: env = smooth(|x|)
//   Rms  mode: env = sqrt(smooth(x²))   <- attack / release act on x², so when x² reaches 63% the amplitude is only
//                                         at sqrt(0.632) ≈ 79.5%; when thinking in terms of an "RMS window", you
//                                         would usually set attack = release = the window length.
class EnvelopeFollower {
public:
    void prepare(double sampleRate) {
        filter_.prepare(sampleRate);
        reset();
    }

    void reset() noexcept {
        filter_.reset();
        envelope_ = 0.0f;
    }

    void setAttackMs(float ms) noexcept { filter_.setAttackMs(ms); }
    void setReleaseMs(float ms) noexcept { filter_.setReleaseMs(ms); }
    void setMode(DetectorMode mode) noexcept { mode_ = mode; }
    void setSmoothingMode(SmoothingMode mode) noexcept { filter_.setMode(mode); }

    float getAttackMs() const noexcept { return filter_.getAttackMs(); }
    float getReleaseMs() const noexcept { return filter_.getReleaseMs(); }
    DetectorMode getMode() const noexcept { return mode_; }
    SmoothingMode getSmoothingMode() const noexcept { return filter_.getMode(); }

    // The current envelope (linear amplitude); does not change the state
    float getEnvelope() const noexcept { return envelope_; }

    float processSample(float x) noexcept {
        // Bad-input guard: Inf / NaN are treated as 0; very large values are first clamped to ±kMaxInput so that x²
        // cannot overflow to Inf (x² exceeds the float limit once |x| is above about 1.8e19). Normal audio is far
        // below this value and is unaffected.
        if (!std::isfinite(x))
            x = 0.0f;
        x = std::clamp(x, -kMaxInput, kMaxInput);
        if (mode_ == DetectorMode::Peak) {
            envelope_ = filter_.process(std::abs(x));
        } else {
            const float meanSquare = filter_.process(x * x);
            envelope_ = std::sqrt(std::max(0.0f, meanSquare));
        }
        return envelope_;
    }

private:
    static constexpr float kMaxInput = 1.0e6f; // +120 dBFS, x² = 1e12, comfortably within the float range

    AttackReleaseFilter filter_;
    DetectorMode mode_ = DetectorMode::Peak;
    float envelope_ = 0.0f;
};

} // namespace dawlab::dsp
