#pragma once
// Biquad: a second-order IIR filter (two poles, two zeros). The EQ, crossover,
// de-esser, reverb damping and more throughout this course are all built on it.
//
// General form (a0 normalized to 1):
//     y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
//
//               b0 + b1 z^-1 + b2 z^-2
//     H(z) = ----------------------------
//               1  + a1 z^-1 + a2 z^-2
//
// The coefficients come from the static factories of BiquadCoefficients, whose formulas are
// from Robert Bristow-Johnson's "Cookbook formulae for audio EQ biquad filter coefficients"
// (the RBJ Audio EQ Cookbook): analog prototype -> bilinear transform (with frequency
// prewarping) -> digital coefficients.
//
// The Biquad class is implemented in Transposed Direct Form II (TDF-II): each channel has only
// two state variables, the state is of the same magnitude as the output (no internal
// amplification as in DF-II), and the output stays smooth when the coefficients change.
// Precision comes from using double for both coefficients and state: changing only one of the
// two is not enough (Chapter 02 README section 2.15 and the l02_direct_forms example measure this).

#include <dawlab/DspMath.h>
#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>
#include <vector>

namespace dawlab::dsp {

// Butterworth design: when an N-th order lowpass / highpass is split into floor(N/2) cascaded
// biquads (plus one first-order stage for odd N), this returns the Q of the stage-th biquad.
// The N poles of a Butterworth filter are equally spaced on the left half of a circle in the
// s-plane; a conjugate pole pair makes an angle φ_k with the negative real axis, and the
// corresponding biquad is s² + 2cos(φ_k) s + 1, so Q_k = 1 / (2 cos φ_k):
//     N even: φ_k = (2k + 1) π / (2N), k = 0 .. N/2 - 1
//     N odd:  φ_k = (k + 1) π / N,     k = 0 .. (N-3)/2   (the real pole at φ = 0 is handled by one first-order stage)
// N = 2 -> 0.7071; N = 3 -> 1.0; N = 4 -> 0.5412, 1.3066; N = 5 -> 0.6180, 1.6180;
// N = 6 -> 0.5176, 0.7071, 1.9319; N = 8 -> 0.5098, 0.6013, 0.9000, 2.5629.
// The first-order stage for odd orders must be the bilinear version (its -3 dB point lands
// exactly on fc), with the same cutoff as the biquads.
// When stage is out of range (or order < 2) it returns Q = 1/√2 (a legal but meaningless
// value), and never returns inf or a negative value.
inline double butterworthQ(int order, int stage) noexcept {
    const int numBiquads = order / 2;
    if (order < 2 || stage < 0 || stage >= numBiquads)
        return 0.70710678118654752;
    const double phi = (order % 2 == 0) ? (2.0 * stage + 1.0) * kPi / (2.0 * order) // even order
                                        : (stage + 1.0) * kPi / order;              // odd order
    return 1.0 / (2.0 * std::cos(phi));
}

struct BiquadCoefficients {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0; // feed-forward (zeros)
    double a1 = 0.0, a2 = 0.0;           // feedback (poles), a0 = 1

    static constexpr double kButterworthQ = 0.70710678118654752; // 1/√2, second-order Butterworth

    // ---- RBJ Audio EQ Cookbook ----------------------------------------------------
    // Shared intermediate quantities: ω0 = 2π f / fs, α = sin(ω0) / (2Q), A = 10^(gainDb / 40)
    static BiquadCoefficients makeLowpass(double sampleRate, double frequency, double q) noexcept {
        const Angles w = angles(sampleRate, frequency, q);
        return normalize((1.0 - w.cosw) * 0.5, 1.0 - w.cosw, (1.0 - w.cosw) * 0.5, 1.0 + w.alpha, -2.0 * w.cosw,
                         1.0 - w.alpha);
    }

    static BiquadCoefficients makeHighpass(double sampleRate, double frequency, double q) noexcept {
        const Angles w = angles(sampleRate, frequency, q);
        return normalize((1.0 + w.cosw) * 0.5, -(1.0 + w.cosw), (1.0 + w.cosw) * 0.5, 1.0 + w.alpha,
                         -2.0 * w.cosw, 1.0 - w.alpha);
    }

    // Bandpass with a fixed 0 dB peak (constant 0 dB peak gain)
    static BiquadCoefficients makeBandpass(double sampleRate, double frequency, double q) noexcept {
        const Angles w = angles(sampleRate, frequency, q);
        return normalize(w.alpha, 0.0, -w.alpha, 1.0 + w.alpha, -2.0 * w.cosw, 1.0 - w.alpha);
    }

    static BiquadCoefficients makeNotch(double sampleRate, double frequency, double q) noexcept {
        const Angles w = angles(sampleRate, frequency, q);
        return normalize(1.0, -2.0 * w.cosw, 1.0, 1.0 + w.alpha, -2.0 * w.cosw, 1.0 - w.alpha);
    }

    // Allpass: 0 dB at all frequencies, changes only the phase (phase = -180° at f)
    static BiquadCoefficients makeAllpass(double sampleRate, double frequency, double q) noexcept {
        const Angles w = angles(sampleRate, frequency, q);
        return normalize(1.0 - w.alpha, -2.0 * w.cosw, 1.0 + w.alpha, 1.0 + w.alpha, -2.0 * w.cosw,
                         1.0 - w.alpha);
    }

    // Peaking EQ (bell): gain of gainDb at f, returning to 0 dB away from f
    static BiquadCoefficients makePeak(double sampleRate, double frequency, double q, double gainDb) noexcept {
        const Angles w = angles(sampleRate, frequency, q);
        const double A = gainToA(gainDb);
        return normalize(1.0 + w.alpha * A, -2.0 * w.cosw, 1.0 - w.alpha * A, 1.0 + w.alpha / A, -2.0 * w.cosw,
                         1.0 - w.alpha / A);
    }

    // Low shelf: gain of gainDb below f. q = 1/√2 corresponds to the cookbook's shelf slope S = 1 (steepest without overshoot)
    static BiquadCoefficients makeLowShelf(double sampleRate, double frequency, double q, double gainDb) noexcept {
        const Angles w = angles(sampleRate, frequency, q);
        const double A = gainToA(gainDb);
        const double sa = 2.0 * std::sqrt(A) * w.alpha;
        return normalize(A * ((A + 1.0) - (A - 1.0) * w.cosw + sa), 2.0 * A * ((A - 1.0) - (A + 1.0) * w.cosw),
                         A * ((A + 1.0) - (A - 1.0) * w.cosw - sa), (A + 1.0) + (A - 1.0) * w.cosw + sa,
                         -2.0 * ((A - 1.0) + (A + 1.0) * w.cosw), (A + 1.0) + (A - 1.0) * w.cosw - sa);
    }

    // High shelf: gain of gainDb above f
    static BiquadCoefficients makeHighShelf(double sampleRate, double frequency, double q, double gainDb) noexcept {
        const Angles w = angles(sampleRate, frequency, q);
        const double A = gainToA(gainDb);
        const double sa = 2.0 * std::sqrt(A) * w.alpha;
        return normalize(A * ((A + 1.0) + (A - 1.0) * w.cosw + sa), -2.0 * A * ((A - 1.0) + (A + 1.0) * w.cosw),
                         A * ((A + 1.0) + (A - 1.0) * w.cosw - sa), (A + 1.0) - (A - 1.0) * w.cosw + sa,
                         2.0 * ((A - 1.0) - (A + 1.0) * w.cosw), (A + 1.0) - (A - 1.0) * w.cosw - sa);
    }

    // ---- Frequency response --------------------------------------------------------
    // Substitute z = e^{jω} (ω = 2π f / fs) into H(z) to get the complex response at that
    // frequency (magnitude = gain, angle = phase)
    std::complex<double> response(double frequency, double sampleRate) const noexcept {
        const double w = kTwoPi * frequency / sampleRate;
        const std::complex<double> z1 = std::polar(1.0, -w);  // z^-1
        const std::complex<double> z2 = std::polar(1.0, -2.0 * w); // z^-2
        const std::complex<double> num = b0 + b1 * z1 + b2 * z2;
        const std::complex<double> den = 1.0 + a1 * z1 + a2 * z2;
        return num / den;
    }

    double magnitudeDb(double frequency, double sampleRate) const noexcept {
        return gainToDb(std::abs(response(frequency, sampleRate)));
    }

    // Phase (radians, -π..π)
    double phase(double frequency, double sampleRate) const noexcept {
        return std::arg(response(frequency, sampleRate));
    }

    // ---- Stability -----------------------------------------------------------------
    // The two roots (poles) of the denominator 1 + a1 z^-1 + a2 z^-2 = 0: p = (-a1 ± sqrt(a1² - 4 a2)) / 2
    std::pair<std::complex<double>, std::complex<double>> poles() const noexcept {
        const std::complex<double> disc = std::sqrt(std::complex<double>(a1 * a1 - 4.0 * a2, 0.0));
        return {(-a1 + disc) * 0.5, (-a1 - disc) * 0.5};
    }

    // Stable <=> both poles lie inside the unit circle (|p| < 1).
    // For a second-order system this is equivalent to the triangle condition: |a2| < 1 and |a1| < 1 + a2
    bool isStable() const noexcept { return std::abs(a2) < 1.0 && std::abs(a1) < 1.0 + a2; }

private:
    struct Angles {
        double cosw, alpha;
    };

    static Angles angles(double sampleRate, double frequency, double q) noexcept {
        // NaN must be blocked first: std::clamp(NaN, lo, hi) and std::max(NaN, lo) both return NaN
        // unchanged, and once a coefficient becomes NaN even reset() cannot recover it.
        // A NaN frequency is treated as 1 kHz and a NaN Q as 1/√2.
        if (std::isnan(frequency))
            frequency = 1000.0;
        if (std::isnan(q))
            q = kButterworthQ;
        // Limit to (0, Nyquist); Q must not be 0
        const double f = std::clamp(frequency, 1.0e-3, sampleRate * 0.499);
        const double w0 = kTwoPi * f / sampleRate;
        const double safeQ = std::max(q, 1.0e-3);
        return {std::cos(w0), std::sin(w0) / (2.0 * safeQ)};
    }

    // A = 10^(gainDb / 40); a NaN gainDb is treated as 0 dB (A = 1) so the coefficients never become NaN
    static double gainToA(double gainDb) noexcept { return std::isnan(gainDb) ? 1.0 : std::pow(10.0, gainDb / 40.0); }

    // Divide out a0 so the difference equation needs no extra division
    static BiquadCoefficients normalize(double b0, double b1, double b2, double a0, double a1, double a2) noexcept {
        const double inv = 1.0 / a0;
        BiquadCoefficients c;
        c.b0 = b0 * inv;
        c.b1 = b1 * inv;
        c.b2 = b2 * inv;
        c.a1 = a1 * inv;
        c.a2 = a2 * inv;
        return c;
    }
};

class Biquad {
public:
    void prepare(double /*sampleRate*/, int /*maxBlockSize*/, int numChannels) {
        state_.assign(static_cast<size_t>(numChannels), State{});
    }

    void reset() noexcept {
        for (auto& s : state_)
            s = State{};
    }

    // Changing the coefficients does not clear the state, so updating them between blocks
    // causes no click (the state is continuous). Large jumps (for example the cutoff jumping from
    // 100 Hz to 10 kHz in an instant, or a very high Q) can still cause a brief transient. When you
    // need smoothing, smooth the *parameters* (cutoff, gain, ...) and recompute the coefficients
    // in small, frequent steps (the Chapter 03 ParametricEq recomputes every 32 samples; see
    // Chapter 03 section 2.9 for why the coefficients are not interpolated directly).
    void setCoefficients(const BiquadCoefficients& c) noexcept { coeffs_ = c; }
    const BiquadCoefficients& getCoefficients() const noexcept { return coeffs_; }

    // Transposed Direct Form II:
    //     y[n] = b0 x[n] + s1[n-1]
    //     s1[n] = b1 x[n] - a1 y[n] + s2[n-1]
    //     s2[n] = b2 x[n] - a2 y[n]
    float processSample(int channel, float x) noexcept {
        State& s = state_[static_cast<size_t>(channel)];
        const double in = x;
        const double y = coeffs_.b0 * in + s.s1;
        // A single NaN / Inf entering a recursive structure turns its state into NaN permanently
        // (every later output is NaN). On a non-finite value, clear this channel's state and
        // output 0; the filter recovers automatically from the next sample.
        if (!std::isfinite(y)) {
            s = State{};
            return 0.0f;
        }
        s.s1 = flushDenormal(coeffs_.b1 * in - coeffs_.a1 * y + s.s2);
        s.s2 = flushDenormal(coeffs_.b2 * in - coeffs_.a2 * y);
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
        double s1 = 0.0, s2 = 0.0;
    };

    static double flushDenormal(double v) noexcept { return std::abs(v) < 1.0e-30 ? 0.0 : v; }

    BiquadCoefficients coeffs_;
    std::vector<State> state_;
};

} // namespace dawlab::dsp
