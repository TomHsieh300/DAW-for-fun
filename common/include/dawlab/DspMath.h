#pragma once
// Common math constants and conversion functions.

#include <algorithm>
#include <cmath>

namespace dawlab {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// Anything at or below this dB value is treated as silence (-inf dB)
constexpr float kMinusInfinityDb = -100.0f;

// value if it is finite, otherwise fallback. A stateless function (a coefficient factory, a delay-line read) has no
// last value to keep, so a non-finite argument means a documented default.
template <typename T>
inline T finiteOr(T value, T fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

// The rule for an input sample that an object keeps in its memory (filter state, delay line, FIFO, analyzer ring):
// a non-finite value (NaN, +-inf) is never kept. Every later operation on NaN gives NaN (even 0 * NaN), so one bad
// sample would otherwise come back forever; a store writes 0 in its place, and the bad sample is forgotten.
inline float finiteOrZero(float x) noexcept { return finiteOr(x, 0.0f); }

// The rule for a value that a public setter in dsp/ receives. A finite value is used. NaN and +inf are ignored: the
// setter returns and keeps its last valid value (std::clamp(NaN, lo, hi) returns the NaN, std::max(0, NaN) returns 0,
// and a NaN in a smoother or a coefficient stays there). -inf is ignored too, except for a value in dB, where it
// means silence: it becomes the floor kMinusInfinityDb (so Gain::setGainDb(-inf) still mutes). Every float / double
// setter starts with
//     if (!acceptParameter(hz)) return;
//     if (!acceptParameter(db, ParameterUnit::Decibels)) return;
// and dsp_selftest checks it for every setter.
enum class ParameterUnit { Plain, Decibels };

template <typename T>
inline bool acceptParameter(T& value, ParameterUnit unit = ParameterUnit::Plain) noexcept {
    if (std::isfinite(value))
        return true;
    if (unit == ParameterUnit::Decibels && value < T(0)) {
        value = static_cast<T>(kMinusInfinityDb);
        return true;
    }
    return false;
}

// The rule for a range in dB (how far a gate, expander or de-esser attenuates), on top of acceptParameter's: the
// magnitude counts, either sign, and it is capped at 100 dB (-kMinusInfinityDb), where dbToGain already gives silence.
// A larger range cannot be heard, but an infinite one (or one of 1e30 dB) broke the gate's attack ramp. Turns db
// into that magnitude in [0, 100] and returns true, or returns false when the setter must ignore the value:
//     if (!acceptRangeDb(db)) return;
//     rangeDb_ = -db;
inline bool acceptRangeDb(float& db) noexcept {
    if (!acceptParameter(db, ParameterUnit::Decibels))
        return false;
    db = std::min(std::abs(db), -kMinusInfinityDb);
    return true;
}

// The rule for a one-pole smoother whose state is a value the code applies (a gain, a mix, a length, a delay, a
// frequency), whether its target is a setting or a gain computed from the signal: it arrives. One step is
//     y = smoothToward(y, target, k);      // y + k * (target - y)
// and it returns the target itself once the step lands within epsilon of it, or once float rounding no longer moves y
// (k * (target - y) is below half an ulp of y). Without the rule a glide toward 0 parks at a subnormal number (10 to
// 100 times slower on x86 without FTZ/DAZ) and a glide toward any other value parks a hair short of it forever
// (chapter 01, exercise 3). A follower of |x| or x^2 (an envelope follower, a meter) keeps its own rule: tiny state
// is flushed to 0, because an absolute 1e-6 would cut off a quiet signal.
constexpr float kSmootherArriveEpsilon = 1.0e-6f;

inline float smoothToward(float current, float target, float k, float epsilon = kSmootherArriveEpsilon) noexcept {
    const float next = current + k * (target - current);
    return (next == current || std::abs(target - next) < epsilon) ? target : next;
}

// dB -> linear gain: gain = 10^(dB/20)
inline float dbToGain(float db, float minusInfinityDb = kMinusInfinityDb) noexcept {
    return db > minusInfinityDb ? std::pow(10.0f, db * 0.05f) : 0.0f;
}

// Linear gain -> dB: dB = 20 * log10(gain)
inline float gainToDb(float gain, float minusInfinityDb = kMinusInfinityDb) noexcept {
    return gain > 0.0f ? std::max(minusInfinityDb, 20.0f * std::log10(gain)) : minusInfinityDb;
}

inline double dbToGain(double db, double minusInfinityDb = kMinusInfinityDb) noexcept {
    return db > minusInfinityDb ? std::pow(10.0, db * 0.05) : 0.0;
}

inline double gainToDb(double gain, double minusInfinityDb = kMinusInfinityDb) noexcept {
    return gain > 0.0 ? std::max(minusInfinityDb, 20.0 * std::log10(gain)) : minusInfinityDb;
}

// Keeps integer calls such as dbToGain(-6) from being an ambiguous overload
inline float dbToGain(int db) noexcept { return dbToGain(static_cast<float>(db)); }

template <typename T>
constexpr T lerp(T a, T b, T t) noexcept {
    return a + (b - a) * t;
}

inline int msToSamples(double ms, double sampleRate) noexcept {
    return static_cast<int>(std::lround(ms * 0.001 * sampleRate));
}

inline double samplesToMs(double samples, double sampleRate) noexcept {
    return samples * 1000.0 / sampleRate;
}

} // namespace dawlab
