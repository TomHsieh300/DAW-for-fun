#pragma once
// StateVariableFilter (SVF, TPT / "zero-delay feedback" version).
//
// A supplementary component for Chapter 02: when parameters change quickly (for example an
// LFO sweeping the cutoff, or a synthesizer filter envelope), a biquad's coefficients no
// longer match its state, which produces transients and even sudden pops. Discretizing the
// analog state-variable filter with the topology-preserving transform (TPT) makes each state
// variable a real physical quantity (the capacitor voltage of an integrator), so cutoff / Q
// can be changed on every sample, and the same set of states yields the lowpass / bandpass /
// highpass / notch outputs at once.
//
// Derivation: Vadim Zavalishin, "The Art of VA Filter Design" (2nd-order TPT SVF). The
// implementation is equivalent to Andrew Simper's (Cytomic) "Solving the continuous SVF
// equations using trapezoidal integration":
//     g  = tan(π fc / fs)         (the bilinear prewarp)
//     k  = 1 / Q                  (damping)
//     a1 = 1 / (1 + g (g + k)),  a2 = g a1,  a3 = g a2
// For each sample:
//     v3 = x - ic2eq
//     v1 = a1 ic1eq + a2 v3            (bandpass integrator output)
//     v2 = ic2eq + a2 ic1eq + a3 v3    (lowpass integrator output)
//     ic1eq = 2 v1 - ic1eq,  ic2eq = 2 v2 - ic2eq
//     low = v2,  band = v1,  high = x - k v1 - v2,  notch = x - k v1
// The magnitude response is identical to the RBJ cookbook LPF / HPF / BPF / Notch (same
// analog prototype + bilinear transform).

#include <dawlab/DspMath.h>
#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

class StateVariableFilter {
public:
    enum class Type { Lowpass, Highpass, Bandpass, Notch };

    // All outputs from one set of states at once (for example for a crossover or a morphing filter)
    struct Outputs {
        float low, band, high, notch;
    };

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

    // Safe to call on every sample (costs one tan)
    void setCutoff(float hz) noexcept {
        cutoffHz_ = hz;
        updateCoefficients();
    }
    float getCutoff() const noexcept { return cutoffHz_; }

    void setQ(float q) noexcept {
        q_ = std::max(q, 0.025f);
        updateCoefficients();
    }
    float getQ() const noexcept { return q_; }

    Outputs processSampleAll(int channel, float x) noexcept {
        State& s = state_[static_cast<size_t>(channel)];
        const double v3 = x - s.ic2eq;
        const double v1 = a1_ * s.ic1eq + a2_ * v3;
        const double v2 = s.ic2eq + a2_ * s.ic1eq + a3_ * v3;
        s.ic1eq = flushDenormal(2.0 * v1 - s.ic1eq);
        s.ic2eq = flushDenormal(2.0 * v2 - s.ic2eq);
        Outputs o;
        o.low = static_cast<float>(v2);
        o.band = static_cast<float>(k_ * v1); // multiplying by k makes the bandpass peak 0 dB (same as RBJ)
        o.high = static_cast<float>(x - k_ * v1 - v2);
        o.notch = static_cast<float>(x - k_ * v1);
        return o;
    }

    float processSample(int channel, float x) noexcept {
        const Outputs o = processSampleAll(channel, x);
        switch (type_) {
        case Type::Lowpass: return o.low;
        case Type::Highpass: return o.high;
        case Type::Bandpass: return o.band;
        case Type::Notch: return o.notch;
        }
        return o.low;
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
        double ic1eq = 0.0, ic2eq = 0.0; // the "capacitor voltages" of the two integrators
    };

    static double flushDenormal(double v) noexcept { return std::abs(v) < 1.0e-30 ? 0.0 : v; }

    void updateCoefficients() noexcept {
        if (sampleRate_ <= 0.0)
            return;
        const double fc = std::clamp(static_cast<double>(cutoffHz_), 1.0e-3, sampleRate_ * 0.49);
        const double g = std::tan(kPi * fc / sampleRate_);
        k_ = 1.0 / q_;
        a1_ = 1.0 / (1.0 + g * (g + k_));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

    Type type_ = Type::Lowpass;
    double sampleRate_ = 0.0;
    float cutoffHz_ = 1000.0f;
    float q_ = 0.70710678f;
    double k_ = 1.0, a1_ = 0.0, a2_ = 0.0, a3_ = 0.0;
    std::vector<State> state_;
};

} // namespace dawlab::dsp
