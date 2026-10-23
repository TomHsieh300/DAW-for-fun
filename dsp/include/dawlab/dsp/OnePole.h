#pragma once
// OnePole: a first-order (one-pole) lowpass / highpass filter.
//
// This is the simplest and most widely used IIR filter: each sample costs only two
// multiplications (one multiply-add plus one multiply). Its slope is 6 dB/oct
// (-20 dB/decade) and it has no resonance. Typical uses:
//   - parameter smoothing, and the ballistics of an envelope follower
//   - damping inside delay / reverb feedback loops
//   - removing DC / very low frequencies from a signal (highpass)
//
// Lowpass difference equation:
//     y[n] = (1 - a) * x[n] + a * y[n-1]
// The coefficient a is set by the cutoff (the impulse response e^{-t/τ} of an analog RC
// lowpass decays by a factor of a per sample):
//     a = exp(-2π fc / fs)
// This version (impulse-invariant / "exact") puts the -3 dB point at fc when fc << fs;
// near Nyquist the -3 dB point lands too high (the Chapter 02 README gives the numbers).
// If you need the -3 dB point to land exactly on fc, use the bilinear version instead
// (the README and the l02_filter_response example compare the two).
//
// The highpass is a normalized first-order highpass (zero at DC, 0 dB at Nyquist):
//     y[n] = h * (x[n] - x[n-1]) + a * y[n-1],   h = (1 + a) / 2
// It differs from "x - lowpass" only by a constant gain (the latter has a high-frequency
// gain of 2a/(1+a), which falls short of 0 dB when the cutoff is high).

#include <dawlab/DspMath.h>
#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

class OnePole {
public:
    enum class Type { Lowpass, Highpass };

    // Computes the feedback coefficient a = exp(-2π fc / fs) from the cutoff and sample rate.
    // Provided for exercises and examples (to check hand calculations).
    static double coefficientForCutoff(double cutoffHz, double sampleRate) noexcept {
        return std::exp(-kTwoPi * cutoffHz / sampleRate);
    }

    void prepare(double sampleRate, int /*maxBlockSize*/, int numChannels) {
        sampleRate_ = sampleRate;
        state_.assign(static_cast<size_t>(numChannels), State{});
        updateCoefficients();
    }

    void reset() noexcept {
        for (auto& s : state_)
            s = State{};
    }

    void setType(Type type) noexcept { type_ = type; }
    Type getType() const noexcept { return type_; }

    // NaN is ignored (the last valid cutoff is kept): std::clamp(NaN, lo, hi) returns NaN
    // unchanged, and once a coefficient becomes NaN even reset() cannot recover it.
    // ±inf is handled by the clamp in updateCoefficients().
    void setCutoff(float hz) noexcept {
        if (std::isnan(hz))
            return;
        cutoffHz_ = hz;
        updateCoefficients();
    }
    float getCutoff() const noexcept { return cutoffHz_; }

    // The current feedback coefficient a (0 < a < 1)
    double getCoefficient() const noexcept { return a_; }

    float processSample(int channel, float x) noexcept {
        State& s = state_[static_cast<size_t>(channel)];
        double y;
        if (type_ == Type::Lowpass) {
            // y[n] = (1 - a) x[n] + a y[n-1]
            y = b_ * x + a_ * s.y1;
        } else {
            // y[n] = h (x[n] - x[n-1]) + a y[n-1]
            y = h_ * (x - s.x1) + a_ * s.y1;
            s.x1 = x;
        }
        s.y1 = flushDenormal(y);
        return static_cast<float>(y);
    }

    void process(AudioBlock block) noexcept {
        const int numChannels = std::min(block.getNumChannels(), static_cast<int>(state_.size()));
        for (int ch = 0; ch < numChannels; ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = processSample(ch, d[i]);
        }
    }

private:
    struct State {
        double x1 = 0.0; // x[n-1] (used only by the highpass)
        double y1 = 0.0; // y[n-1]
    };

    // The state of a recursive structure approaches 0 forever without ever reaching it, and
    // eventually falls into the denormal range (the CPU becomes very slow).
    // Flush values too small to be audible straight to 0.
    static double flushDenormal(double v) noexcept { return std::abs(v) < 1.0e-30 ? 0.0 : v; }

    void updateCoefficients() noexcept {
        if (sampleRate_ <= 0.0)
            return;
        // Clamp the cutoff to (0, Nyquist) so that a never reaches 0 or 1, which would make the
        // filter either do nothing or blow up
        const double fc = std::clamp(static_cast<double>(cutoffHz_), 1.0e-3, sampleRate_ * 0.49);
        a_ = coefficientForCutoff(fc, sampleRate_);
        b_ = 1.0 - a_;
        h_ = 0.5 * (1.0 + a_);
    }

    Type type_ = Type::Lowpass;
    double sampleRate_ = 0.0;
    float cutoffHz_ = 1000.0f;
    double a_ = 0.0, b_ = 1.0, h_ = 0.5;
    std::vector<State> state_;
};

} // namespace dawlab::dsp
