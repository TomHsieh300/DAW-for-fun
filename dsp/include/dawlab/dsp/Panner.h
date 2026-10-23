#pragma once
// Panner：把 pan 位置（-1 = 全左、0 = 中央、+1 = 全右）換成左右兩個 gain，再乘進左右聲道。
//
// 令 θ = (pan + 1) / 2 ∈ [0, 1]（0 = 全左、0.5 = 中央、1 = 全右），三種 pan law：
//
//   Linear        : gL = 1 − θ               gR = θ
//                   中央 0.5 = −6.02 dB；gL + gR = 1（振幅和固定）；gL² + gR² 在中央 = 0.5（功率掉 3 dB）
//   ConstantPower : gL = cos(θ·π/2)          gR = sin(θ·π/2)
//                   （程式裡 gL 寫成 sin((1−θ)·π/2)，數學上相同，但 pan = ±1 時另一側精確為 0）
//                   中央 1/√2 = −3.01 dB；gL² + gR² = 1 永遠成立（功率和固定）
//   Minus4_5dB    : gL = √(Linear_L · ConstantPower_L)，gR 同理（兩者的幾何平均）
//                   中央 √(0.5 · 0.7071) = 0.5946 = −4.52 dB，是兩種 law 的折衷
//
// 立體聲與 mono 的差別（請務必理解）：
// - process(block) 把 gL 乘進 channel 0、gR 乘進 channel 1。
//   * 若 block 的左右內容相同（mono 訊號複製到兩聲道），這就是真正的 mono pan：聲像移到左右之間。
//   * 若 block 是真正的立體聲（左右不同），這其實是 balance：只是把其中一邊變小，
//     右邊的內容不會「搬」到左邊。DAW 的立體聲 track 多半就是這麼做的。
// - processMonoToStereo(mono, stereoOut) 才是「mono 輸入 → 立體聲輸出」的 pan。
// - block 只有 1 個 channel 時 process() 什麼都不做（沒有第二個聲道可以 pan）。
//
// 左右 gain 各自用 SmoothedValue 線性平滑（預設 50 ms），所以 pan 掃動不會有 zipper noise。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

enum class PanLaw { Linear, ConstantPower, Minus4_5dB };

inline const char* panLawName(PanLaw law) noexcept {
    switch (law) {
    case PanLaw::Linear: return "Linear";
    case PanLaw::ConstantPower: return "ConstantPower";
    case PanLaw::Minus4_5dB: return "Minus4_5dB";
    }
    return "?";
}

class Panner {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        (void)numChannels;
        sampleRate_ = sampleRate;
        left_.reset(sampleRate, rampSeconds_);
        right_.reset(sampleRate, rampSeconds_);
        snapToTarget();
    }

    // 讓左右 gain 直接對齊目標（不 ramp）
    void reset() noexcept { snapToTarget(); }

    // pan ∈ [-1, 1]：-1 全左、0 中央、+1 全右（超出範圍會被夾住）
    // 參數 setter 必須在 block 之間、由呼叫 process() 的同一個 thread 呼叫（本章元件不是 thread-safe，
    // 跨 thread 的參數傳遞見第 13、14 章）。NaN / inf 不是合法位置，會被忽略（保持原值）。
    void setPan(float pan) noexcept {
        if (!std::isfinite(pan))
            return;
        pan_ = std::clamp(pan, -1.0f, 1.0f);
        updateTargets();
    }
    float getPan() const noexcept { return pan_; }

    void setPanLaw(PanLaw law) noexcept {
        law_ = law;
        updateTargets();
    }
    PanLaw getPanLaw() const noexcept { return law_; }

    void setRampSeconds(double seconds) noexcept {
        rampSeconds_ = std::max(0.0, seconds);
        if (sampleRate_ > 0.0) {
            const int n = static_cast<int>(std::lround(rampSeconds_ * sampleRate_));
            left_.setRampLength(n);
            right_.setRampLength(n);
        }
    }

    // 目前實際使用的左右 gain（ramp 中會介於新舊值之間）
    float getCurrentLeftGain() const noexcept { return left_.getCurrentValue(); }
    float getCurrentRightGain() const noexcept { return right_.getCurrentValue(); }
    bool isSmoothing() const noexcept { return left_.isSmoothing() || right_.isSmoothing(); }

    // 純函式：由 pan 與 law 算出左右 gain（線性倍率）。範例的 pan law 表格就是直接呼叫它。
    static void computeGains(float pan, PanLaw law, float& left, float& right) noexcept {
        // NaN / inf 視為中央，保證輸出的 gain 一定是有限值
        const float p = std::isfinite(pan) ? std::clamp(pan, -1.0f, 1.0f) : 0.0f;
        const float theta = 0.5f * (p + 1.0f); // 0 = 全左, 0.5 = 中央, 1 = 全右
        const float linL = 1.0f - theta;
        const float linR = theta;
        // cos(θ·π/2) 寫成 sin((1−θ)·π/2)：float(π)/2 比真正的 π/2 大一點點，
        // 直接算 cos(float(π)/2) 會得到 −4.37e-8（負值、不是 0），全右時左聲道殘留反相訊號。
        // 改用 sin 之後兩端都是 sin(0) = 0（精確），而且左右對稱、永不為負。
        const float halfPi = static_cast<float>(kPi) * 0.5f;
        const float cpL = std::sin((1.0f - theta) * halfPi);
        const float cpR = std::sin(theta * halfPi);
        switch (law) {
        case PanLaw::Linear:
            left = linL;
            right = linR;
            break;
        case PanLaw::ConstantPower:
            left = cpL;
            right = cpR;
            break;
        case PanLaw::Minus4_5dB:
            // 幾何平均：dB 值剛好是兩種 law 的平均（-6.02 與 -3.01 的平均 = -4.52 dB）
            left = std::sqrt(linL * cpL);
            right = std::sqrt(linR * cpR);
            break;
        }
    }

    // in-place：channel 0 乘 gL、channel 1 乘 gR（兩聲道內容相同 = mono pan；不同 = balance）
    void process(AudioBlock block) noexcept {
        if (block.getNumChannels() < 2)
            return;
        left_.applyGain(block.getChannelPointer(0), block.getNumSamples());
        right_.applyGain(block.getChannelPointer(1), block.getNumSamples());
    }

    // mono 輸入 → 立體聲輸出：stereoOut[0] = mono · gL，stereoOut[1] = mono · gR
    // mono 可以與 stereoOut 的 channel 0 是同一塊記憶體（會先算右聲道再算左聲道）。
    void processMonoToStereo(const float* mono, AudioBlock stereoOut) noexcept {
        if (stereoOut.getNumChannels() < 2)
            return;
        const int n = stereoOut.getNumSamples();
        float* outL = stereoOut.getChannelPointer(0);
        float* outR = stereoOut.getChannelPointer(1);
        if (!left_.isSmoothing() && !right_.isSmoothing()) {
            const float gL = left_.getTargetValue();
            const float gR = right_.getTargetValue();
            for (int i = 0; i < n; ++i) {
                const float x = mono[i];
                outR[i] = x * gR;
                outL[i] = x * gL;
            }
            return;
        }
        for (int i = 0; i < n; ++i) {
            const float x = mono[i];
            const float gL = left_.getNextValue();
            const float gR = right_.getNextValue();
            outR[i] = x * gR;
            outL[i] = x * gL;
        }
    }

private:
    void updateTargets() noexcept {
        float l = 0.0f, r = 0.0f;
        computeGains(pan_, law_, l, r);
        left_.setTargetValue(l);
        right_.setTargetValue(r);
    }

    void snapToTarget() noexcept {
        float l = 0.0f, r = 0.0f;
        computeGains(pan_, law_, l, r);
        left_.setCurrentAndTargetValue(l);
        right_.setCurrentAndTargetValue(r);
    }

    double sampleRate_ = 0.0;
    double rampSeconds_ = 0.05;
    float pan_ = 0.0f;
    PanLaw law_ = PanLaw::ConstantPower;
    SmoothedValue left_{0.70710678f};
    SmoothedValue right_{0.70710678f};
};

} // namespace dawlab::dsp
