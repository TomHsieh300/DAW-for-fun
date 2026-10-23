#pragma once
// Biquad：二階 IIR filter（two poles, two zeros），整套教材的 EQ、crossover、
// de-esser、reverb damping……都建立在它之上。
//
// 一般式（a0 已正規化為 1）：
//     y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
//
//               b0 + b1 z^-1 + b2 z^-2
//     H(z) = ----------------------------
//               1  + a1 z^-1 + a2 z^-2
//
// 係數由 BiquadCoefficients 的 static factory 產生，公式來自 Robert Bristow-Johnson 的
// "Cookbook formulae for audio EQ biquad filter coefficients"（RBJ Audio EQ Cookbook）：
// analog prototype → bilinear transform（含 frequency prewarping）→ 數位係數。
//
// Biquad class 用 Transposed Direct Form II（TDF-II）實作：每個 channel 只有兩個狀態變數，
// 狀態和輸出同量級（沒有 DF-II 的內部放大），換係數時輸出平順。精度則靠「係數與狀態都用 double」：
// 只換其中一個不夠（第 02 章 README 2.15 節與範例 l02_direct_forms 有實測）。

#include <dawlab/DspMath.h>
#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>
#include <vector>

namespace dawlab::dsp {

// Butterworth 設計：N 階 lowpass / highpass 拆成 floor(N/2) 個 biquad 串接（奇數階再加一級一階）時，
// 第 k 級 biquad 的 Q 值。Butterworth 的 N 個 pole 等距分布在 s 平面左半圓上；
// 一對共軛 pole 與負實軸的夾角為 φ_k，對應的 biquad 是 s² + 2cos(φ_k) s + 1，所以 Q_k = 1 / (2 cos φ_k)：
//     N 偶數：φ_k = (2k + 1) π / (2N)，k = 0 .. N/2 - 1
//     N 奇數：φ_k = (k + 1) π / N，    k = 0 .. (N-3)/2   （φ = 0 的那個實數 pole 由一級 one-pole 負責）
// N = 2 → 0.7071；N = 3 → 1.0；N = 4 → 0.5412, 1.3066；N = 5 → 0.6180, 1.6180；
// N = 6 → 0.5176, 0.7071, 1.9319；N = 8 → 0.5098, 0.6013, 0.9000, 2.5629。
// 奇數階那一級 one-pole 必須是 bilinear 版（-3 dB 點精準在 fc）、cutoff 與 biquad 相同。
// stage 超出範圍（或 order < 2）時回傳 Q = 1/√2（合法但無意義的值），不會回傳 inf / 負值。
inline double butterworthQ(int order, int stage) noexcept {
    const int numBiquads = order / 2;
    if (order < 2 || stage < 0 || stage >= numBiquads)
        return 0.70710678118654752;
    const double phi = (order % 2 == 0) ? (2.0 * stage + 1.0) * kPi / (2.0 * order) // 偶數階
                                        : (stage + 1.0) * kPi / order;              // 奇數階
    return 1.0 / (2.0 * std::cos(phi));
}

struct BiquadCoefficients {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0; // feed-forward（zeros）
    double a1 = 0.0, a2 = 0.0;           // feedback（poles），a0 = 1

    static constexpr double kButterworthQ = 0.70710678118654752; // 1/√2，二階 Butterworth

    // ---- RBJ Audio EQ Cookbook ----------------------------------------------------
    // 共用的中間量：ω0 = 2π f / fs，α = sin(ω0) / (2Q)，A = 10^(gainDb / 40)
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

    // Bandpass，峰值固定 0 dB（constant 0 dB peak gain）
    static BiquadCoefficients makeBandpass(double sampleRate, double frequency, double q) noexcept {
        const Angles w = angles(sampleRate, frequency, q);
        return normalize(w.alpha, 0.0, -w.alpha, 1.0 + w.alpha, -2.0 * w.cosw, 1.0 - w.alpha);
    }

    static BiquadCoefficients makeNotch(double sampleRate, double frequency, double q) noexcept {
        const Angles w = angles(sampleRate, frequency, q);
        return normalize(1.0, -2.0 * w.cosw, 1.0, 1.0 + w.alpha, -2.0 * w.cosw, 1.0 - w.alpha);
    }

    // Allpass：全頻段 0 dB，只改相位（在 f 處相位 = -180°）
    static BiquadCoefficients makeAllpass(double sampleRate, double frequency, double q) noexcept {
        const Angles w = angles(sampleRate, frequency, q);
        return normalize(1.0 - w.alpha, -2.0 * w.cosw, 1.0 + w.alpha, 1.0 + w.alpha, -2.0 * w.cosw,
                         1.0 - w.alpha);
    }

    // Peaking EQ（bell）：f 處增益 gainDb，遠離 f 回到 0 dB
    static BiquadCoefficients makePeak(double sampleRate, double frequency, double q, double gainDb) noexcept {
        const Angles w = angles(sampleRate, frequency, q);
        const double A = gainToA(gainDb);
        return normalize(1.0 + w.alpha * A, -2.0 * w.cosw, 1.0 - w.alpha * A, 1.0 + w.alpha / A, -2.0 * w.cosw,
                         1.0 - w.alpha / A);
    }

    // Low shelf：f 以下增益 gainDb。q = 1/√2 對應 cookbook 的 shelf slope S = 1（最陡但不過衝）
    static BiquadCoefficients makeLowShelf(double sampleRate, double frequency, double q, double gainDb) noexcept {
        const Angles w = angles(sampleRate, frequency, q);
        const double A = gainToA(gainDb);
        const double sa = 2.0 * std::sqrt(A) * w.alpha;
        return normalize(A * ((A + 1.0) - (A - 1.0) * w.cosw + sa), 2.0 * A * ((A - 1.0) - (A + 1.0) * w.cosw),
                         A * ((A + 1.0) - (A - 1.0) * w.cosw - sa), (A + 1.0) + (A - 1.0) * w.cosw + sa,
                         -2.0 * ((A - 1.0) + (A + 1.0) * w.cosw), (A + 1.0) + (A - 1.0) * w.cosw - sa);
    }

    // High shelf：f 以上增益 gainDb
    static BiquadCoefficients makeHighShelf(double sampleRate, double frequency, double q, double gainDb) noexcept {
        const Angles w = angles(sampleRate, frequency, q);
        const double A = gainToA(gainDb);
        const double sa = 2.0 * std::sqrt(A) * w.alpha;
        return normalize(A * ((A + 1.0) + (A - 1.0) * w.cosw + sa), -2.0 * A * ((A - 1.0) + (A + 1.0) * w.cosw),
                         A * ((A + 1.0) + (A - 1.0) * w.cosw - sa), (A + 1.0) - (A - 1.0) * w.cosw + sa,
                         2.0 * ((A - 1.0) - (A + 1.0) * w.cosw), (A + 1.0) - (A - 1.0) * w.cosw - sa);
    }

    // ---- 頻率響應 ------------------------------------------------------------------
    // 把 z = e^{jω}（ω = 2π f / fs）代入 H(z)，得到該頻率的複數響應（大小 = 增益，角度 = 相位）
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

    // 相位（弧度，-π..π）
    double phase(double frequency, double sampleRate) const noexcept {
        return std::arg(response(frequency, sampleRate));
    }

    // ---- 穩定性 --------------------------------------------------------------------
    // 分母 1 + a1 z^-1 + a2 z^-2 = 0 的兩個根（poles）：p = (-a1 ± sqrt(a1² - 4 a2)) / 2
    std::pair<std::complex<double>, std::complex<double>> poles() const noexcept {
        const std::complex<double> disc = std::sqrt(std::complex<double>(a1 * a1 - 4.0 * a2, 0.0));
        return {(-a1 + disc) * 0.5, (-a1 - disc) * 0.5};
    }

    // 穩定 ⇔ 兩個 pole 都在單位圓內（|p| < 1）。
    // 對二階系統等價於三角形條件：|a2| < 1 且 |a1| < 1 + a2
    bool isStable() const noexcept { return std::abs(a2) < 1.0 && std::abs(a1) < 1.0 + a2; }

private:
    struct Angles {
        double cosw, alpha;
    };

    static Angles angles(double sampleRate, double frequency, double q) noexcept {
        // NaN 要先擋：std::clamp(NaN, lo, hi) 與 std::max(NaN, lo) 都會原樣回傳 NaN，
        // 係數一旦變成 NaN，之後就算 reset() 也救不回來。NaN frequency 當成 1 kHz、NaN Q 當成 1/√2。
        if (std::isnan(frequency))
            frequency = 1000.0;
        if (std::isnan(q))
            q = kButterworthQ;
        // 限制在 (0, Nyquist)，Q 不可為 0
        const double f = std::clamp(frequency, 1.0e-3, sampleRate * 0.499);
        const double w0 = kTwoPi * f / sampleRate;
        const double safeQ = std::max(q, 1.0e-3);
        return {std::cos(w0), std::sin(w0) / (2.0 * safeQ)};
    }

    // A = 10^(gainDb / 40)；NaN gainDb 當成 0 dB（A = 1），避免係數變成 NaN
    static double gainToA(double gainDb) noexcept { return std::isnan(gainDb) ? 1.0 : std::pow(10.0, gainDb / 40.0); }

    // 把 a0 除掉，讓 difference equation 不用再除一次
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

    // 換係數不會清掉狀態，所以在 block 之間更新係數時不會有 click（狀態是連續的）。
    // 但大幅跳動（例如 cutoff 瞬間從 100 Hz 跳到 10 kHz、或 Q 很高）仍可能有短暫的 transient，
    // 需要平滑時請平滑「參數」（cutoff、gain……），再以小步、頻繁地重算係數
    // （第 03 章 ParametricEq 每 32 個 sample 重算一次；為什麼不直接插值係數見第 03 章 2.9 節）。
    void setCoefficients(const BiquadCoefficients& c) noexcept { coeffs_ = c; }
    const BiquadCoefficients& getCoefficients() const noexcept { return coeffs_; }

    // Transposed Direct Form II：
    //     y[n] = b0 x[n] + s1[n-1]
    //     s1[n] = b1 x[n] - a1 y[n] + s2[n-1]
    //     s2[n] = b2 x[n] - a2 y[n]
    float processSample(int channel, float x) noexcept {
        State& s = state_[static_cast<size_t>(channel)];
        const double in = x;
        const double y = coeffs_.b0 * in + s.s1;
        // 只要進來一個 NaN / Inf，遞迴狀態就會永久變成 NaN（之後每個輸出都是 NaN）。
        // 遇到非有限值就清掉這個 channel 的狀態、輸出 0，下一個 sample 起自動恢復。
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
