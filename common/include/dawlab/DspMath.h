#pragma once
// 常用的數學常數與轉換函式。

#include <algorithm>
#include <cmath>

namespace dawlab {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// 低於這個 dB 值一律視為靜音（-inf dB）
constexpr float kMinusInfinityDb = -100.0f;

// dB -> 線性倍率：gain = 10^(dB/20)
inline float dbToGain(float db, float minusInfinityDb = kMinusInfinityDb) noexcept {
    return db > minusInfinityDb ? std::pow(10.0f, db * 0.05f) : 0.0f;
}

// 線性倍率 -> dB：dB = 20 * log10(gain)
inline float gainToDb(float gain, float minusInfinityDb = kMinusInfinityDb) noexcept {
    return gain > 0.0f ? std::max(minusInfinityDb, 20.0f * std::log10(gain)) : minusInfinityDb;
}

inline double dbToGain(double db, double minusInfinityDb = kMinusInfinityDb) noexcept {
    return db > minusInfinityDb ? std::pow(10.0, db * 0.05) : 0.0;
}

inline double gainToDb(double gain, double minusInfinityDb = kMinusInfinityDb) noexcept {
    return gain > 0.0 ? std::max(minusInfinityDb, 20.0 * std::log10(gain)) : minusInfinityDb;
}

// 讓 dbToGain(-6) 這種整數寫法不會出現 overload 模稜兩可（ambiguous）
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
