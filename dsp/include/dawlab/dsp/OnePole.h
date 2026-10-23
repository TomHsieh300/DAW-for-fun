#pragma once
// OnePole：一階（one-pole）lowpass / highpass filter。
//
// 這是最簡單、也最常用的 IIR filter：每個 sample 只要 2 個乘法（一次乘加加一個乘法），
// 斜率 6 dB/oct（-20 dB/decade），沒有 resonance。常見用途：
//   - 參數平滑（smoothing）、envelope follower 的 ballistics
//   - delay / reverb feedback 迴路裡的 damping
//   - 把訊號的 DC / 極低頻去掉（highpass）
//
// Lowpass 的 difference equation：
//     y[n] = (1 - a) * x[n] + a * y[n-1]
// 係數 a 由 cutoff 決定（analog RC lowpass 的 impulse response e^{-t/τ} 每個 sample 衰減 a 倍）：
//     a = exp(-2π fc / fs)
// 這個版本（impulse-invariant / "exact" 版）在 fc << fs 時 -3 dB 點就在 fc；
// fc 接近 Nyquist 時會偏高（第 02 章 README 有數字）。想要 -3 dB 點精準落在 fc，
// 可以改用 bilinear 版本（README 與範例 l02_filter_response 有比較）。
//
// Highpass 用的是正規化後的一階 highpass（DC 處為 0、Nyquist 處為 0 dB）：
//     y[n] = h * (x[n] - x[n-1]) + a * y[n-1],   h = (1 + a) / 2
// 它和「x - lowpass」只差一個常數增益（後者在高頻的增益是 2a/(1+a)，cutoff 高時會不到 0 dB）。

#include <dawlab/DspMath.h>
#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

class OnePole {
public:
    enum class Type { Lowpass, Highpass };

    // 由 cutoff 與 sample rate 算出 feedback 係數 a = exp(-2π fc / fs)。
    // 給練習與範例用（手算驗證）。
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

    // NaN 會被忽略（保留上一個有效的 cutoff）：std::clamp(NaN, lo, hi) 會原樣回傳 NaN，
    // 係數一旦變成 NaN，之後就算 reset() 也救不回來。±inf 則由 updateCoefficients() 的 clamp 處理。
    void setCutoff(float hz) noexcept {
        if (std::isnan(hz))
            return;
        cutoffHz_ = hz;
        updateCoefficients();
    }
    float getCutoff() const noexcept { return cutoffHz_; }

    // 目前的 feedback 係數 a（0 < a < 1）
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
        double x1 = 0.0; // x[n-1]（只有 highpass 用到）
        double y1 = 0.0; // y[n-1]
    };

    // 遞迴結構的狀態會無限趨近 0 但永遠不等於 0，最後落入 denormal 範圍（CPU 會變得非常慢）。
    // 把小到聽不見的值直接清成 0。
    static double flushDenormal(double v) noexcept { return std::abs(v) < 1.0e-30 ? 0.0 : v; }

    void updateCoefficients() noexcept {
        if (sampleRate_ <= 0.0)
            return;
        // cutoff 限制在 (0, Nyquist) 之間，避免 a 落到 0 或 1 造成 filter 完全不動或爆掉
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
