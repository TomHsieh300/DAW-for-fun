#pragma once
// StateVariableFilter（SVF，TPT / "zero-delay feedback" 版本）。
//
// 第 02 章的補充元件：biquad 的係數在參數快速變化時（例如 LFO 掃 cutoff、
// 合成器的 filter envelope）會因為「係數跟狀態不匹配」而產生 transient 甚至瞬間爆音；
// 用 topology-preserving transform（TPT）把 analog state-variable filter 離散化後，
// 狀態變數對應的是真實的物理量（積分器的電容電壓），cutoff / Q 每個 sample 都能改，
// 而且同一組狀態同時給出 lowpass / bandpass / highpass / notch 輸出。
//
// 推導來源：Vadim Zavalishin, "The Art of VA Filter Design"（2nd-order TPT SVF），
// 實作上與 Andrew Simper（Cytomic）的 "Solving the continuous SVF equations using
// trapezoidal integration" 相同：
//     g  = tan(π fc / fs)         （bilinear 的 prewarp）
//     k  = 1 / Q                  （damping）
//     a1 = 1 / (1 + g (g + k)),  a2 = g a1,  a3 = g a2
// 每個 sample：
//     v3 = x - ic2eq
//     v1 = a1 ic1eq + a2 v3            （bandpass 積分器輸出）
//     v2 = ic2eq + a2 ic1eq + a3 v3    （lowpass 積分器輸出）
//     ic1eq = 2 v1 - ic1eq,  ic2eq = 2 v2 - ic2eq
//     low = v2,  band = v1,  high = x - k v1 - v2,  notch = x - k v1
// 大小響應和 RBJ cookbook 的 LPF / HPF / BPF / Notch 完全一樣（同一個 analog prototype + bilinear）。

#include <dawlab/DspMath.h>
#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

class StateVariableFilter {
public:
    enum class Type { Lowpass, Highpass, Bandpass, Notch };

    // 一次拿到同一組狀態的所有輸出（例如做 crossover、或 morphing filter）
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

    // 可以每個 sample 呼叫（一次 tan）
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
        o.band = static_cast<float>(k_ * v1); // 乘 k 讓 bandpass 峰值 = 0 dB（與 RBJ 相同）
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
        double ic1eq = 0.0, ic2eq = 0.0; // 兩個積分器的「電容電壓」
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
