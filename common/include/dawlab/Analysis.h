#pragma once
// Analysis tools: peak, RMS, single-frequency amplitude (Goertzel), frequency-response measurement, harmonics / THD.
// They let the exercise solutions verify a DSP result with numbers rather than by ear alone.

#include "Console.h"
#include "AudioBuffer.h"
#include "DspMath.h"

#include <cmath>
#include <vector>

namespace dawlab {

inline float peakAbs(const float* x, int n) noexcept {
    float p = 0.0f;
    for (int i = 0; i < n; ++i)
        p = std::max(p, std::abs(x[i]));
    return p;
}

// Largest |sample| across all channels
inline float peakAbs(const AudioBuffer& b) noexcept {
    float p = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        p = std::max(p, peakAbs(b.getReadPointer(ch), b.getNumSamples()));
    return p;
}

inline float rms(const float* x, int n) noexcept {
    if (n <= 0)
        return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += double(x[i]) * x[i];
    return static_cast<float>(std::sqrt(sum / n));
}

inline float rms(const AudioBuffer& b, int channel) noexcept {
    return rms(b.getReadPointer(channel), b.getNumSamples());
}

inline bool hasNanOrInf(const AudioBuffer& b) noexcept {
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (!std::isfinite(b.getSample(ch, i)))
                return true;
    return false;
}

// Estimates the amplitude of the sine component at frequency freq using the Goertzel algorithm (with a Hann window).
// For a sine wave of amplitude A the return value is approximately A. Handy for measuring a filter's gain at a given frequency.
inline double goertzelAmplitude(const float* x, int n, double freq, double sampleRate) noexcept {
    if (n <= 1)
        return 0.0;
    const double w = kTwoPi * freq / sampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0, windowSum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double win = 0.5 - 0.5 * std::cos(kTwoPi * i / (n - 1));
        windowSum += win;
        const double s0 = x[i] * win + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return 2.0 * std::sqrt(std::max(0.0, power)) / windowSum;
}

// Measures the gain (in dB) at freq of any system that processes one sample at a time.
// processSample is a float(float) callable, for example [&](float x){ return filter.processSample(0, x); }
// It first feeds skipSeconds of signal so the transient dies away, then measures the remainder.
template <typename ProcessFn>
double measureGainDb(ProcessFn&& processSample, double freq, double sampleRate, double seconds = 0.5,
                     double skipSeconds = 0.2, float amplitude = 0.25f) {
    const int skip = static_cast<int>(skipSeconds * sampleRate);
    const int n = static_cast<int>(seconds * sampleRate);
    std::vector<float> in(static_cast<size_t>(n)), out(static_cast<size_t>(n));
    for (int i = 0; i < skip + n; ++i) {
        const auto x = static_cast<float>(amplitude * std::sin(kTwoPi * freq * i / sampleRate));
        const float y = processSample(x);
        if (i >= skip) {
            in[static_cast<size_t>(i - skip)] = x;
            out[static_cast<size_t>(i - skip)] = y;
        }
    }
    const double a = goertzelAmplitude(in.data(), n, freq, sampleRate);
    const double b = goertzelAmplitude(out.data(), n, freq, sampleRate);
    return gainToDb(b / a);
}

// Returns the level in dB of harmonics 1..numHarmonics relative to the fundamental (index 0 = fundamental = 0 dB).
// Useful for analysing which harmonics (odd vs even) saturation / distortion generates.
inline std::vector<double> harmonicLevelsDb(const float* x, int n, double fundamental, double sampleRate,
                                            int numHarmonics) {
    std::vector<double> levels;
    const double h1 = goertzelAmplitude(x, n, fundamental, sampleRate);
    for (int h = 1; h <= numHarmonics; ++h) {
        const double f = fundamental * h;
        if (f >= sampleRate * 0.5)
            break;
        levels.push_back(gainToDb(goertzelAmplitude(x, n, f, sampleRate) / h1));
    }
    return levels;
}

// Total harmonic distortion (THD), in %: sqrt(H2² + H3² + ... ) / H1
inline double thdPercent(const float* x, int n, double fundamental, double sampleRate, int numHarmonics = 10) {
    const double h1 = goertzelAmplitude(x, n, fundamental, sampleRate);
    double sum = 0.0;
    for (int h = 2; h <= numHarmonics; ++h) {
        const double f = fundamental * h;
        if (f >= sampleRate * 0.5)
            break;
        const double a = goertzelAmplitude(x, n, f, sampleRate);
        sum += a * a;
    }
    return h1 > 0.0 ? 100.0 * std::sqrt(sum) / h1 : 0.0;
}

} // namespace dawlab
