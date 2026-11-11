#pragma once
// Step 4's WSOLA, solved: Exercise 4 starts from it. (Do Step 4 first: this file gives it away.)
//
//   stretch(x, positions, settings)   output sample n sounds like the input around positions[n], at its own pitch:
//                                     Hann pieces every window / 2 output samples, each slid by up to `search`
//                                     samples to where its first half looks most like the natural continuation of
//                                     the piece before (bestOffset()).
//   measureTone(y, hz)                what is not a tone of `hz` in y, in dB below it, and its pitch in cents
//                                     (stretches of 4096 samples with a free phase, as Step 4's check measures).

#include <dawlab/DspMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace wsola {

constexpr double kRate = 48000.0;

struct StretchSettings {
    int window = 1024; // samples of a piece (even); a new piece every window / 2 output samples
    int search = 256;  // how far a piece may slide either way; 0: plain OLA
};

inline float inputAt(const std::vector<float>& x, std::int64_t i) noexcept {
    return i >= 0 && i < static_cast<std::int64_t>(x.size()) ? x[static_cast<size_t>(i)] : 0.0f;
}

inline double hann(int i, int window) noexcept {
    return 0.5 - 0.5 * std::cos(dawlab::kTwoPi * i / window);
}

inline double positionAt(const std::vector<double>& positions, std::int64_t n) noexcept {
    const std::int64_t size = static_cast<std::int64_t>(positions.size());
    if (size == 0)
        return 0.0;
    if (size == 1)
        return positions[0] + static_cast<double>(n);
    if (n < 0)
        return positions[0] + static_cast<double>(n) * (positions[1] - positions[0]);
    if (n >= size)
        return positions[static_cast<size_t>(size - 1)] +
               static_cast<double>(n - (size - 1)) * (positions[static_cast<size_t>(size - 1)] - positions[static_cast<size_t>(size - 2)]);
    return positions[static_cast<size_t>(n)];
}

// The score of a candidate start against the natural continuation: sum a b / sqrt(sum a a) over a half window.
inline double scoreAt(const std::vector<float>& x, std::int64_t start, std::int64_t natural, int half) noexcept {
    double cross = 0.0, energy = 0.0;
    for (int i = 0; i < half; ++i) {
        const double a = inputAt(x, start + i), b = inputAt(x, natural + i);
        cross += a * b;
        energy += a * a;
    }
    return energy > 0.0 ? cross / std::sqrt(energy) : 0.0;
}

inline int bestOffset(const std::vector<float>& x, std::int64_t candidate, std::int64_t natural, const StretchSettings& settings) noexcept {
    if (candidate == natural || settings.search <= 0)
        return 0;
    const int half = settings.window / 2;
    int best = 0;
    double bestScore = scoreAt(x, candidate, natural, half);
    for (int distance = 1; distance <= settings.search; ++distance)
        for (const int d : {distance, -distance}) {
            const double s = scoreAt(x, candidate + d, natural, half);
            if (s > bestScore) {
                bestScore = s;
                best = d;
            }
        }
    return best;
}

inline std::vector<float> stretch(const std::vector<float>& x, const std::vector<double>& positions, const StretchSettings& settings = {}) {
    if (settings.window < 4 || settings.window % 2 != 0 || settings.search < 0)
        return {};
    const std::int64_t length = static_cast<std::int64_t>(positions.size());
    const int half = settings.window / 2;
    std::vector<double> sum(positions.size(), 0.0);
    bool first = true;
    std::int64_t before = 0;
    for (std::int64_t at = -half; at < length; at += half) {
        const std::int64_t candidate = std::llround(positionAt(positions, at + half)) - half;
        const std::int64_t start = first ? candidate : candidate + bestOffset(x, candidate, before + half, settings);
        for (int i = 0; i < settings.window; ++i)
            if (at + i >= 0 && at + i < length)
                sum[static_cast<size_t>(at + i)] += hann(i, settings.window) * inputAt(x, start + i);
        before = start;
        first = false;
    }
    std::vector<float> out(sum.size());
    for (size_t n = 0; n < sum.size(); ++n)
        out[n] = static_cast<float>(sum[n]);
    return out;
}

// ---- measuring ------------------------------------------------------------------------------------------------

inline std::vector<float> sine(double hz, int samples, double amplitude) {
    std::vector<float> s(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i)
        s[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(dawlab::kTwoPi * hz * i / kRate));
    return s;
}

// Output sample n sounds like input sample n x speed.
inline std::vector<double> steady(double speed, int outputSamples) {
    std::vector<double> p(static_cast<size_t>(outputSamples));
    for (int n = 0; n < outputSamples; ++n)
        p[static_cast<size_t>(n)] = n * speed;
    return p;
}

struct Tone {
    double noiseDb = -200.0; // what is not the tone, in dB below it
    double cents = 0.0;      // the pitch against `hz`
};

inline Tone measureTone(const std::vector<float>& y, size_t from, size_t to, double hz) {
    constexpr size_t kStretch = 4096;
    const double w = dawlab::kTwoPi * hz / kRate;
    double signal = 0.0, noise = 0.0, turned = 0.0, before = 0.0;
    size_t stretches = 0;
    to = std::min(to, y.size());
    for (size_t s0 = from; s0 + kStretch <= to; s0 += kStretch) {
        double ss = 0.0, cc = 0.0, sc = 0.0, ys = 0.0, yc = 0.0;
        for (size_t i = s0; i < s0 + kStretch; ++i) {
            const double sn = std::sin(w * static_cast<double>(i)), cs = std::cos(w * static_cast<double>(i));
            ss += sn * sn, cc += cs * cs, sc += sn * cs, ys += y[i] * sn, yc += y[i] * cs;
        }
        const double det = ss * cc - sc * sc;
        if (std::fabs(det) < 1e-12)
            continue;
        const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
        for (size_t i = s0; i < s0 + kStretch; ++i) {
            const double fit = a * std::sin(w * static_cast<double>(i)) + b * std::cos(w * static_cast<double>(i));
            signal += fit * fit;
            noise += (y[i] - fit) * (y[i] - fit);
        }
        const double phase = std::atan2(b, a);
        if (stretches > 0) {
            double step = phase - before;
            while (step > dawlab::kPi)
                step -= dawlab::kTwoPi;
            while (step < -dawlab::kPi)
                step += dawlab::kTwoPi;
            turned += step;
        }
        before = phase;
        ++stretches;
    }
    Tone tone;
    if (signal > 0.0 && stretches > 1) {
        tone.noiseDb = 10.0 * std::log10(signal / std::max(noise, 1e-300));
        const double cyclesPerSample = turned / (dawlab::kTwoPi * static_cast<double>((stretches - 1) * kStretch));
        tone.cents = 1200.0 * std::log2(1.0 + cyclesPerSample * kRate / hz);
    }
    return tone;
}

} // namespace wsola
