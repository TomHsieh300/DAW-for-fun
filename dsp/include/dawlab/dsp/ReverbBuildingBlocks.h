#pragma once
// Chapter 12: teaching-oriented building blocks for reverb.
//
//   CombFilter      feedback comb (the "lowpass comb" of Schroeder / Freeverb): produces density and decay
//   AllpassDiffuser Schroeder allpass: flat magnitude, only scrambles the phase; used to raise echo density (diffusion)
//   TappedDelay     multi-tap delay line: early reflections
//   measureRt60()   estimates RT60 from an impulse response (windowed-RMS envelope + linear regression)
//
// These components are all mono and process one sample at a time, and are deliberately kept short so they are
// easy to compare against the formulas in the README.
// Their internal ring buffers are the same idea as the DelayLine of chapters 05 / 09 (write index + modulo);
// we keep a private copy here so each building block can be read on its own.
// Reverb.h (the main FDN implementation) uses chapter 09's FractionalDelayLine and chapter 02's OnePole directly.

#include <dawlab/DspMath.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace dawlab::dsp {

namespace reverb_detail {

// A minimal mono ring buffer: after push, read(0) is the value just written and read(D) is the value from D samples ago.
class MonoRing {
public:
    void prepare(int maxDelaySamples) {
        size_ = std::max(2, maxDelaySamples + 1);
        buffer_.assign(static_cast<size_t>(size_), 0.0f);
        write_ = 0;
    }
    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        write_ = 0;
    }
    void push(float x) noexcept {
        buffer_[static_cast<size_t>(write_)] = x;
        write_ = (write_ + 1) % size_;
    }
    float read(int delaySamples) const noexcept {
        assert(delaySamples >= 0 && delaySamples < size_);
        int i = write_ - 1 - delaySamples;
        if (i < 0)
            i += size_;
        return buffer_[static_cast<size_t>(i)];
    }
    int capacity() const noexcept { return size_ - 1; }

private:
    std::vector<float> buffer_{0.0f, 0.0f};
    int size_ = 2;
    int write_ = 0;
};

// Turn tiny values (in the denormal range) straight into 0, so recursive structures do not make the CPU spike when the tail dies away
inline float flushDenormal(float x) noexcept { return std::abs(x) < 1e-20f ? 0.0f : x; }

} // namespace reverb_detail

// ---------------------------------------------------------------------------
// Feedback comb filter (with damping)
//
//   y[n] = x[n - D] + g * lp(y[n - D])          lp = one-pole lowpass (damping)
//
// Without damping the impulse response is delta[n-D] + g delta[n-2D] + g^2 delta[n-3D] + ...
// Each trip around the loop (D samples) attenuates by a factor of g, so the time RT60 needed to decay to
// -60 dB satisfies
//   20*log10(g^(RT60*fs/D)) = -60   =>   g = 10^(-3*D / (RT60*fs))
// ---------------------------------------------------------------------------
class CombFilter {
public:
    void prepare(int maxDelaySamples) {
        ring_.prepare(maxDelaySamples);
        delay_ = std::min(delay_, ring_.capacity());
        reset();
    }

    void reset() noexcept {
        ring_.reset();
        lowpassState_ = 0.0f;
    }

    // Loop length D (in samples). Changing it makes the impulse response jump immediately, so do not adjust it continuously during playback.
    void setDelaySamples(int d) noexcept { delay_ = std::clamp(d, 1, ring_.capacity()); }
    int getDelaySamples() const noexcept { return delay_; }

    // Feedback amount g (0..0.9999)
    void setFeedback(float g) noexcept { feedback_ = std::clamp(g, 0.0f, 0.9999f); }
    float getFeedback() const noexcept { return feedback_; }

    // damping 0..1: 0 = no filtering (a pure comb); the larger it is, the faster high frequencies decay in the loop.
    // Same as Freeverb: filtered = out*(1-damp) + filtered*damp
    void setDamping(float d) noexcept { damping_ = std::clamp(d, 0.0f, 0.999f); }

    float processSample(float x) noexcept {
        const float delayed = ring_.read(delay_ - 1); // read the value from D samples ago (the index only lines up after the push below)
        lowpassState_ = reverb_detail::flushDenormal(delayed * (1.0f - damping_) + lowpassState_ * damping_);
        const float out = delayed;
        ring_.push(x + feedback_ * lowpassState_);
        return out;
    }

    // g = 10^(-3*D / (RT60*fs))
    static float feedbackForRt60(int delaySamples, double rt60Seconds, double sampleRate) noexcept {
        if (rt60Seconds <= 0.0)
            return 0.0f;
        return static_cast<float>(std::pow(10.0, -3.0 * delaySamples / (rt60Seconds * sampleRate)));
    }

    // RT60 = -3*D / (fs*log10(g))
    static double rt60ForFeedback(int delaySamples, float feedback, double sampleRate) noexcept {
        if (feedback <= 0.0f)
            return 0.0;
        if (feedback >= 1.0f)
            return 1e30;
        return -3.0 * delaySamples / (sampleRate * std::log10(static_cast<double>(feedback)));
    }

private:
    reverb_detail::MonoRing ring_;
    int delay_ = 1;
    float feedback_ = 0.0f;
    float damping_ = 0.0f;
    float lowpassState_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Schroeder allpass (diffuser)
//
//   v[n] = x[n] + g * v[n - D]
//   y[n] = -g * v[n] + v[n - D]
//
// Transfer function H(z) = (-g + z^-D) / (1 - g z^-D), |H(e^jw)| = 1: the magnitude is flat and only the
// phase changes, so many stages can be cascaded to "scramble" the echoes without changing the timbre.
// ---------------------------------------------------------------------------
class AllpassDiffuser {
public:
    void prepare(int maxDelaySamples) {
        ring_.prepare(maxDelaySamples);
        delay_ = std::min(delay_, ring_.capacity());
        reset();
    }
    void reset() noexcept { ring_.reset(); }

    void setDelaySamples(int d) noexcept { delay_ = std::clamp(d, 1, ring_.capacity()); }
    int getDelaySamples() const noexcept { return delay_; }
    void setGain(float g) noexcept { gain_ = std::clamp(g, -0.999f, 0.999f); }
    float getGain() const noexcept { return gain_; }

    float processSample(float x) noexcept {
        const float delayed = ring_.read(delay_ - 1);
        const float v = reverb_detail::flushDenormal(x + gain_ * delayed);
        ring_.push(v);
        return delayed - gain_ * v;
    }

private:
    reverb_detail::MonoRing ring_;
    int delay_ = 1;
    float gain_ = 0.5f;
};

// ---------------------------------------------------------------------------
// Tapped delay (early reflections)
//
//   y[n] = sum_k gain_k * x[n - delay_k]
//
// Each tap represents one early reflection: the farther the wall, the longer the delay; the more absorptive
// the wall, the smaller the gain.
// ---------------------------------------------------------------------------
class TappedDelay {
public:
    struct Tap {
        int delaySamples;
        float gain;
    };

    void prepare(int maxDelaySamples) {
        ring_.prepare(maxDelaySamples);
        reset();
    }
    void reset() noexcept { ring_.reset(); }

    // May only be called during the prepare phase (not on the audio thread): it allocates
    void setTaps(const std::vector<Tap>& taps) {
        taps_ = taps;
        for (Tap& t : taps_)
            t.delaySamples = std::clamp(t.delaySamples, 0, ring_.capacity());
    }
    const std::vector<Tap>& getTaps() const noexcept { return taps_; }

    float processSample(float x) noexcept {
        ring_.push(x);
        float y = 0.0f;
        for (const Tap& t : taps_)
            y += t.gain * ring_.read(t.delaySamples);
        return y;
    }

private:
    reverb_detail::MonoRing ring_;
    std::vector<Tap> taps_;
};

// ---------------------------------------------------------------------------
// Estimating RT60 from an impulse response (simple version)
//
// 1. Compute an RMS envelope in units of windowMs (in dB, relative to the maximum)
// 2. Take the part after the maximum where the envelope lies between fitStartDb and fitEndDb and run a linear regression
// 3. Extrapolate the slope (dB/s): RT60 = -60 / slope
//
// Exercise 6 does the same thing with the more standard Schroeder backward integration.
// ---------------------------------------------------------------------------
struct DecayMeasurement {
    double rt60Seconds = 0.0;      // estimated RT60
    double slopeDbPerSecond = 0.0; // slope obtained from the regression (negative)
    double peakDb = -200.0;        // maximum of the envelope (dBFS)
    int numPoints = 0;             // number of windows that took part in the regression
    bool valid = false;            // false if there are too few points or the slope is not negative
};

inline DecayMeasurement measureRt60(const float* x, int n, double sampleRate, double windowMs = 10.0,
                                    double fitStartDb = -5.0, double fitEndDb = -35.0) {
    DecayMeasurement m;
    const int w = std::max(1, static_cast<int>(windowMs * 0.001 * sampleRate));
    const int numWindows = n / w;
    if (numWindows < 3)
        return m;

    std::vector<double> envDb(static_cast<size_t>(numWindows));
    int peakIndex = 0;
    for (int k = 0; k < numWindows; ++k) {
        double sum = 0.0;
        for (int i = 0; i < w; ++i) {
            const double v = x[k * w + i];
            sum += v * v;
        }
        const double rmsValue = std::sqrt(sum / w);
        envDb[static_cast<size_t>(k)] = rmsValue > 0.0 ? 20.0 * std::log10(rmsValue) : -200.0;
        if (envDb[static_cast<size_t>(k)] > envDb[static_cast<size_t>(peakIndex)])
            peakIndex = k;
    }
    m.peakDb = envDb[static_cast<size_t>(peakIndex)];

    // Collect the regression points: from the first time after the peak that the envelope falls below fitStartDb,
    // until the first time it falls below fitEndDb
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    int count = 0;
    bool started = false;
    for (int k = peakIndex; k < numWindows; ++k) {
        const double rel = envDb[static_cast<size_t>(k)] - m.peakDb;
        if (!started && rel <= fitStartDb)
            started = true;
        if (!started)
            continue;
        if (rel < fitEndDb)
            break;
        const double t = (k + 0.5) * w / sampleRate;
        sx += t;
        sy += rel;
        sxx += t * t;
        sxy += t * rel;
        ++count;
    }
    m.numPoints = count;
    if (count < 3)
        return m;
    const double denom = count * sxx - sx * sx;
    if (denom <= 0.0)
        return m;
    m.slopeDbPerSecond = (count * sxy - sx * sy) / denom;
    if (m.slopeDbPerSecond >= 0.0)
        return m;
    m.rt60Seconds = -60.0 / m.slopeDbPerSecond;
    m.valid = true;
    return m;
}

// Find the smallest prime >= n (used to make delay lengths mutually coprime; returns 2 for n < 2)
inline int nextPrime(int n) noexcept {
    auto isPrime = [](int v) {
        if (v < 2)
            return false;
        for (int d = 2; d * d <= v; ++d)
            if (v % d == 0)
                return false;
        return true;
    };
    int v = std::max(2, n);
    while (!isPrime(v))
        ++v;
    return v;
}

} // namespace dawlab::dsp
