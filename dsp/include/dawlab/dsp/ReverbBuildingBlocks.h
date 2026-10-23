#pragma once
// 第 12 章：殘響的教學用 building blocks。
//
//   CombFilter      feedback comb（Schroeder / Freeverb 的「lowpass comb」）：產生密度與 decay
//   AllpassDiffuser Schroeder allpass：magnitude 全平、只打散相位，用來增加 echo 密度（diffusion）
//   TappedDelay     多 tap 的 delay line：early reflections（早期反射）
//   measureRt60()   從 impulse response 估 RT60（windowed-RMS envelope + 線性回歸）
//
// 這些元件都是單聲道、逐 sample 處理，刻意寫得很短，方便對照 README 的公式。
// 它們內部的 ring buffer 與第 05 / 09 章的 DelayLine 是同一個概念（寫入指標 + 取模），
// 這裡自己放一份是為了讓每個 building block 可以獨立閱讀。
// Reverb.h（FDN 主實作）則直接使用第 09 章的 FractionalDelayLine 與第 02 章的 OnePole。

#include <dawlab/DspMath.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace dawlab::dsp {

namespace reverb_detail {

// 最小的單聲道 ring buffer：push 之後 read(0) 就是剛寫進去的值，read(D) 是 D 個 sample 前的值。
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

// 把極小的值（denormal 範圍）直接變成 0，避免遞迴結構在尾巴消失時 CPU 飆高
inline float flushDenormal(float x) noexcept { return std::abs(x) < 1e-20f ? 0.0f : x; }

} // namespace reverb_detail

// ---------------------------------------------------------------------------
// Feedback comb filter（含 damping）
//
//   y[n] = x[n - D] + g · lp(y[n - D])          lp = one-pole lowpass（damping）
//
// 沒有 damping 時 impulse response 是 δ[n-D] + g δ[n-2D] + g² δ[n-3D] + ...
// 每繞一圈（D 個 sample）衰減 g 倍，所以衰減到 -60 dB 需要的時間 RT60 滿足
//   20·log10(g^(RT60·fs/D)) = -60   =>   g = 10^(-3·D / (RT60·fs))
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

    // 迴路長度 D（samples）。改變它會讓 impulse response 立刻跳動，不要在播放中連續調整。
    void setDelaySamples(int d) noexcept { delay_ = std::clamp(d, 1, ring_.capacity()); }
    int getDelaySamples() const noexcept { return delay_; }

    // 回授量 g（0..0.9999）
    void setFeedback(float g) noexcept { feedback_ = std::clamp(g, 0.0f, 0.9999f); }
    float getFeedback() const noexcept { return feedback_; }

    // damping 0..1：0 = 不濾波（純 comb），越大高頻在迴路裡衰減越快。
    // 等同 Freeverb：filtered = out·(1-damp) + filtered·damp
    void setDamping(float d) noexcept { damping_ = std::clamp(d, 0.0f, 0.999f); }

    float processSample(float x) noexcept {
        const float delayed = ring_.read(delay_ - 1); // 讀 D 個 sample 前的值（下面 push 之後索引才對齊）
        lowpassState_ = reverb_detail::flushDenormal(delayed * (1.0f - damping_) + lowpassState_ * damping_);
        const float out = delayed;
        ring_.push(x + feedback_ * lowpassState_);
        return out;
    }

    // g = 10^(-3·D / (RT60·fs))
    static float feedbackForRt60(int delaySamples, double rt60Seconds, double sampleRate) noexcept {
        if (rt60Seconds <= 0.0)
            return 0.0f;
        return static_cast<float>(std::pow(10.0, -3.0 * delaySamples / (rt60Seconds * sampleRate)));
    }

    // RT60 = -3·D / (fs·log10(g))
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
// Schroeder allpass（diffuser）
//
//   v[n] = x[n] + g · v[n - D]
//   y[n] = -g · v[n] + v[n - D]
//
// transfer function H(z) = (-g + z^-D) / (1 - g z^-D)，|H(e^jω)| = 1：振幅全平、只改相位，
// 所以可以串很多級來「打散」echo 卻不改變音色。
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
// Tapped delay（early reflections）
//
//   y[n] = Σ_k gain_k · x[n - delay_k]
//
// 每個 tap 代表一次早期反射：牆面越遠 delay 越長、吸音越多 gain 越小。
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

    // 只能在 prepare 階段（非 audio thread）呼叫：會 allocate
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
// 從 impulse response 估 RT60（簡易版）
//
// 1. 以 windowMs 為單位算 RMS envelope（dB，相對最大值）
// 2. 取最大值之後、envelope 介於 fitStartDb 與 fitEndDb 之間的區段做線性回歸
// 3. slope（dB/s）外插：RT60 = -60 / slope
//
// 練習 6 會用更標準的 Schroeder backward integration 做同一件事。
// ---------------------------------------------------------------------------
struct DecayMeasurement {
    double rt60Seconds = 0.0;      // 估計的 RT60
    double slopeDbPerSecond = 0.0; // 回歸得到的斜率（負值）
    double peakDb = -200.0;        // envelope 最大值（dBFS）
    int numPoints = 0;             // 參與回歸的視窗數
    bool valid = false;            // 點數太少或 slope 不是負的 → false
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

    // 收集回歸點：從 peak 之後第一次低於 fitStartDb 開始，到第一次低於 fitEndDb 為止
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

// 找 >= n 的最小質數（delay 長度互質用；n < 2 回傳 2）
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
